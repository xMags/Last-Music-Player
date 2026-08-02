#include "pch.h"
#include "Backend/ProviderClient.h"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <chrono>
#include <string>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        winrt::hstring TrimTrailingSlash(winrt::hstring const& value)
        {
            std::wstring trimmed{ value.c_str() };
            while (!trimmed.empty() && (trimmed.back() == L'/' || trimmed.back() == L'\\'))
            {
                trimmed.pop_back();
            }
            return winrt::hstring{ trimmed };
        }

        winrt::hstring ProviderErrorMessage(uint32_t status, winrt::hstring const& body)
        {
            winrt::hstring code;
            winrt::hstring message;
            winrt::Windows::Data::Json::JsonObject object{ nullptr };
            if (!body.empty() && winrt::Windows::Data::Json::JsonObject::TryParse(body, object))
            {
                auto namedString = [&](wchar_t const* name) -> winrt::hstring
                {
                    if (!object.HasKey(name))
                    {
                        return {};
                    }

                    auto value = object.GetNamedValue(name);
                    return value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String
                        ? value.GetString()
                        : winrt::hstring{};
                };

                code = namedString(L"code");
                if (code.empty())
                {
                    code = namedString(L"error");
                }
                message = namedString(L"message");
            }

            std::wstring text = L"Provider returned " + std::to_wstring(status);
            if (!message.empty())
            {
                text += L": ";
                text += message.c_str();
                if (!code.empty())
                {
                    text += L" (";
                    text += code.c_str();
                    text += L")";
                }
            }
            else if (!body.empty())
            {
                text += L": ";
                text += body.c_str();
            }
            return winrt::hstring{ text };
        }

        winrt::Windows::Foundation::IAsyncAction EnsureProviderSuccessAsync(
            winrt::Windows::Web::Http::HttpResponseMessage const& response)
        {
            if (response.IsSuccessStatusCode())
            {
                co_return;
            }

            auto body = co_await response.Content().ReadAsStringAsync();
            throw winrt::hresult_error(
                E_FAIL,
                ProviderErrorMessage(static_cast<uint32_t>(response.StatusCode()), body));
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage> WithProviderTimeout(
            winrt::Windows::Foundation::IAsyncOperationWithProgress<
                winrt::Windows::Web::Http::HttpResponseMessage,
                winrt::Windows::Web::Http::HttpProgress> operation,
            wchar_t const* operationName)
        {
            auto timer = winrt::Windows::System::Threading::ThreadPoolTimer::CreateTimer(
                [operation](winrt::Windows::System::Threading::ThreadPoolTimer const&)
                {
                    try
                    {
                        operation.Cancel();
                    }
                    catch (...)
                    {
                    }
                },
                std::chrono::seconds(15));

            try
            {
                auto result = co_await operation;
                timer.Cancel();
                co_return result;
            }
            catch (winrt::hresult_canceled const&)
            {
                timer.Cancel();
                throw winrt::hresult_error(E_ABORT, winrt::hstring(operationName) + L" timed out.");
            }
            catch (...)
            {
                timer.Cancel();
                throw;
            }
        }
    }

    ProviderClient::ProviderClient()
    {
        ApplyAuthHeader();
    }

    ProviderClient::ProviderClient(winrt::hstring baseUrl)
        : m_baseUrl(TrimTrailingSlash(baseUrl))
    {
        ApplyAuthHeader();
    }

    void ProviderClient::SetBaseUrl(winrt::hstring const& baseUrl)
    {
        m_baseUrl = TrimTrailingSlash(baseUrl);
    }

    winrt::hstring ProviderClient::GetBaseUrl() const
    {
        return m_baseUrl;
    }

    void ProviderClient::SetBearerToken(winrt::hstring const& token)
    {
        m_token = token;
        ApplyAuthHeader();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::GetProvidersAsync()
    {
        auto response = co_await WithProviderTimeout(
            m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/providers"))),
            L"Provider providers request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<uint32_t> ProviderClient::GetProvidersStatusAsync()
    {
        auto response = co_await WithProviderTimeout(
            m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/providers"))),
            L"Provider status request");
        co_return static_cast<uint32_t>(response.StatusCode());
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::SearchAsync(winrt::hstring query)
    {
        auto escaped = winrt::Windows::Foundation::Uri::EscapeComponent(query);
        auto response = co_await WithProviderTimeout(
            m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/search?q=" + escaped))),
            L"Provider search request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::GetRelatedAsync(winrt::hstring sourceUrl)
    {
        auto escaped = winrt::Windows::Foundation::Uri::EscapeComponent(sourceUrl);
        auto response = co_await WithProviderTimeout(
            m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/related?sourceUrl=" + escaped))),
            L"Provider related request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::ResolveUrlAsync(winrt::hstring url)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(url));

        winrt::Windows::Web::Http::HttpStringContent content{
            payload.Stringify(),
            winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
            L"application/json"
        };

        auto response = co_await WithProviderTimeout(
            m_httpClient.PostAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/resolve")), content),
            L"Provider resolve request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::GetLyricsAsync(
        winrt::hstring artist, winrt::hstring title, winrt::hstring album, int64_t durationMs,
        winrt::hstring sourceUrl)
    {
        std::wstring query = L"/v1/lyrics?artist=";
        query += winrt::Windows::Foundation::Uri::EscapeComponent(artist).c_str();
        query += L"&title=";
        query += winrt::Windows::Foundation::Uri::EscapeComponent(title).c_str();
        if (!album.empty())
        {
            query += L"&album=";
            query += winrt::Windows::Foundation::Uri::EscapeComponent(album).c_str();
        }
        if (durationMs > 0)
        {
            query += L"&durationMs=";
            query += std::to_wstring(durationMs);
        }
        if (!sourceUrl.empty())
        {
            query += L"&sourceUrl=";
            query += winrt::Windows::Foundation::Uri::EscapeComponent(sourceUrl).c_str();
        }

        auto response = co_await WithProviderTimeout(
            m_httpClient.GetAsync(winrt::Windows::Foundation::Uri(BuildUrl(winrt::hstring{ query }))),
            L"Provider lyrics request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::ResolveDiscordArtworkAsync(
        winrt::hstring title, winrt::hstring artist)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"title", winrt::Windows::Data::Json::JsonValue::CreateStringValue(title));
        payload.Insert(L"artist", winrt::Windows::Data::Json::JsonValue::CreateStringValue(artist));

        winrt::Windows::Web::Http::HttpStringContent content{
            payload.Stringify(),
            winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
            L"application/json"
        };

        auto response = co_await WithProviderTimeout(
            m_httpClient.PostAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/discord/artwork-url")), content),
            L"Discord artwork-url resolve");
        co_await EnsureProviderSuccessAsync(response);

        auto body = co_await response.Content().ReadAsStringAsync();
        winrt::Windows::Data::Json::JsonObject obj{ nullptr };
        if (!winrt::Windows::Data::Json::JsonObject::TryParse(body, obj)
            || !obj.HasKey(L"url"))
        {
            co_return winrt::hstring{};
        }
        auto value = obj.GetNamedValue(L"url");
        if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::String)
        {
            co_return winrt::hstring{};
        }
        co_return value.GetString();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> ProviderClient::ImportAlbumAsync(winrt::hstring url)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(url));

        winrt::Windows::Web::Http::HttpStringContent content{
            payload.Stringify(),
            winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
            L"application/json"
        };

        auto response = co_await WithProviderTimeout(
            m_httpClient.PostAsync(winrt::Windows::Foundation::Uri(BuildUrl(L"/v1/import-album")), content),
            L"Provider import request");
        co_await EnsureProviderSuccessAsync(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::hstring ProviderClient::BuildUrl(winrt::hstring const& pathAndQuery) const
    {
        return m_baseUrl + pathAndQuery;
    }

    void ProviderClient::ApplyAuthHeader()
    {
        auto headers = m_httpClient.DefaultRequestHeaders();
        if (headers.HasKey(L"Authorization"))
        {
            headers.Remove(L"Authorization");
        }

        if (!m_token.empty())
        {
            headers.TryAppendWithoutValidation(L"Authorization", L"Bearer " + m_token);
        }
    }
}
