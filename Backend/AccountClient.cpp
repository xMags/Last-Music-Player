#include "pch.h"
#include "Backend/AccountClient.h"

#include "Backend/BuildConfig.h"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Threading.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr HRESULT AccountUnauthorizedHresult = HRESULT_FROM_WIN32(ERROR_LOGON_FAILURE);
        constexpr std::uint64_t MaxArtworkBytes = 12ull * 1024ull * 1024ull;

        winrt::hstring TrimOrigin(winrt::hstring const& origin)
        {
            std::wstring value{ origin.c_str() };
            while (!value.empty() && value.back() == L'/')
            {
                value.pop_back();
            }
            return winrt::hstring{ value };
        }

        // Storefront codes are ISO-3166-style two-letter regions. Anything else
        // is treated as absent and falls back to the service default rather than
        // being forwarded as a query value.
        winrt::hstring NormalizeStorefront(winrt::hstring const& storefront)
        {
            std::wstring value{ storefront.c_str() };
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            bool valid = value.size() == 2
                && std::all_of(value.begin(), value.end(), [](wchar_t ch)
                {
                    return ch >= L'a' && ch <= L'z';
                });
            return valid ? winrt::hstring{ value } : winrt::hstring{ DefaultCatalogStorefront };
        }

        winrt::hstring NormalizeChartType(winrt::hstring const& type)
        {
            if (type == L"albums" || type == L"playlists")
            {
                return type;
            }
            return winrt::hstring{ L"songs" };
        }

        HRESULT StatusHresult(std::uint32_t status)
        {
            if (status == 401) return AccountUnauthorizedHresult;
            if (status == 403) return E_ACCESSDENIED;
            if (status == 404) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
            if (status == 408) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
            if (status == 409) return HRESULT_FROM_WIN32(ERROR_OBJECT_ALREADY_EXISTS);
            if (status == 429) return HRESULT_FROM_WIN32(ERROR_RETRY);
            if (status >= 500) return HRESULT_FROM_WIN32(ERROR_CONNECTION_UNAVAIL);
            return E_FAIL;
        }

        void EnsureAccountSuccess(winrt::Windows::Web::Http::HttpResponseMessage const& response)
        {
            if (response.IsSuccessStatusCode())
            {
                return;
            }
            auto status = static_cast<std::uint32_t>(response.StatusCode());
            throw winrt::hresult_error(StatusHresult(status), SafeAccountErrorMessage(status));
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage> WithAccountTimeout(
            winrt::Windows::Foundation::IAsyncOperationWithProgress<
                winrt::Windows::Web::Http::HttpResponseMessage,
                winrt::Windows::Web::Http::HttpProgress> operation)
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
                std::chrono::seconds(20));

            try
            {
                auto response = co_await operation;
                timer.Cancel();
                co_return response;
            }
            catch (winrt::hresult_canceled const&)
            {
                timer.Cancel();
                throw winrt::hresult_error(
                    HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                    L"Account request timed out.");
            }
            catch (...)
            {
                timer.Cancel();
                throw;
            }
        }
    }

    bool IsTrustedAccountOrigin(winrt::hstring const& origin) noexcept
    {
        try
        {
            if (origin.empty()) return false;
            auto normalized = TrimOrigin(origin);
            winrt::Windows::Foundation::Uri uri{ normalized };
            if (uri.Query() != L"" || uri.Fragment() != L"" || uri.UserName() != L"" || uri.Password() != L"")
            {
                return false;
            }
            auto path = uri.Path();
            if (!path.empty() && path != L"/")
            {
                return false;
            }
            auto host = uri.Host();
            if (uri.SchemeName() == L"https")
            {
                return !host.empty();
            }
            if (uri.SchemeName() != L"http")
            {
                return false;
            }
            return host == L"127.0.0.1"
                || host == L"::1"
                || host == L"[::1]"
                || host == L"localhost";
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::hstring AccountManagementUrl() noexcept
    {
        try
        {
            winrt::hstring origin{ BuildConfig::AccountFrontendOrigin };
            if (!IsTrustedAccountOrigin(origin))
            {
                return {};
            }
            return winrt::hstring{ std::wstring{ TrimOrigin(origin).c_str() } + L"/account" };
        }
        catch (...)
        {
            return {};
        }
    }

    winrt::hstring SafeAccountErrorMessage(std::uint32_t status) noexcept
    {
        switch (status)
        {
        case 400:
            return L"The account request was not accepted.";
        case 401:
            return L"Your account session has expired.";
        case 403:
            return L"This account cannot perform that action.";
        case 404:
            return L"The requested account item was not found.";
        case 409:
            return L"The account library changed. Sync and try again.";
        case 429:
            return L"Too many account requests. Try again shortly.";
        default:
            return status >= 500
                ? winrt::hstring{ L"The account service is temporarily unavailable." }
                : winrt::hstring{ L"The account request failed." };
        }
    }

    bool IsAccountUnauthorized(winrt::hresult_error const& error) noexcept
    {
        return error.code() == AccountUnauthorizedHresult;
    }

    AccountClient::AccountClient()
        : AccountClient(BuildConfig::AccountApiOrigin)
    {
    }

    AccountClient::AccountClient(winrt::hstring const& baseOrigin)
    {
        if (IsTrustedAccountOrigin(baseOrigin))
        {
            m_baseOrigin = TrimOrigin(baseOrigin);
        }
    }

    bool AccountClient::IsConfigured() const noexcept
    {
        return !m_baseOrigin.empty();
    }

    winrt::hstring AccountClient::BaseOrigin() const
    {
        return m_baseOrigin;
    }

    winrt::hstring AccountClient::BuildUrl(winrt::hstring const& trustedPathAndQuery) const
    {
        if (!IsConfigured())
        {
            throw winrt::hresult_error(E_NOTIMPL, L"Account integration is unavailable in this build.");
        }
        if (!trustedPathAndQuery.starts_with(L"/v1/") || trustedPathAndQuery.starts_with(L"//"))
        {
            throw winrt::hresult_invalid_argument(L"Account path is invalid.");
        }
        return m_baseOrigin + trustedPathAndQuery;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Web::Http::HttpResponseMessage> AccountClient::SendAsync(
        winrt::Windows::Web::Http::HttpMethod const& method,
        winrt::hstring const& trustedPathAndQuery,
        winrt::hstring const& bearerSession,
        winrt::hstring const& jsonBody,
        winrt::Windows::Web::Http::HttpCompletionOption completion)
    {
        auto cancellation = co_await winrt::get_cancellation_token();
        cancellation.enable_propagation();

        winrt::Windows::Web::Http::HttpRequestMessage request{
            method,
            winrt::Windows::Foundation::Uri{ BuildUrl(trustedPathAndQuery) }
        };
        request.Headers().TryAppendWithoutValidation(
            L"X-Requested-With",
            BuildConfig::DesktopRequestedWith);
        request.Headers().TryAppendWithoutValidation(L"Accept", L"application/json");
        if (!bearerSession.empty())
        {
            request.Headers().TryAppendWithoutValidation(
                L"Authorization",
                L"Bearer " + bearerSession);
        }
        if (!jsonBody.empty())
        {
            request.Content(winrt::Windows::Web::Http::HttpStringContent{
                jsonBody,
                winrt::Windows::Storage::Streams::UnicodeEncoding::Utf8,
                L"application/json"
            });
        }

        try
        {
            co_return co_await WithAccountTimeout(m_httpClient.SendRequestAsync(request, completion));
        }
        catch (winrt::hresult_error const& error)
        {
            if (error.code() == HRESULT_FROM_WIN32(ERROR_TIMEOUT))
            {
                throw;
            }
            throw winrt::hresult_error(
                HRESULT_FROM_WIN32(ERROR_CONNECTION_UNAVAIL),
                L"The account service is unavailable.");
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::SendJsonAsync(
        winrt::Windows::Web::Http::HttpMethod const& method,
        winrt::hstring const& trustedPathAndQuery,
        winrt::hstring const& bearerSession,
        winrt::hstring const& jsonBody)
    {
        auto response = co_await SendAsync(method, trustedPathAndQuery, bearerSession, jsonBody);
        EnsureAccountSuccess(response);
        co_return co_await response.Content().ReadAsStringAsync();
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::ExchangeCodeAsync(
        winrt::hstring const& code,
        winrt::hstring const& redirectUri,
        winrt::hstring const& verifier)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"grant_type", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"authorization_code"));
        payload.Insert(L"client_id", winrt::Windows::Data::Json::JsonValue::CreateStringValue(BuildConfig::DesktopClientId));
        payload.Insert(L"redirect_uri", winrt::Windows::Data::Json::JsonValue::CreateStringValue(redirectUri));
        payload.Insert(L"code", winrt::Windows::Data::Json::JsonValue::CreateStringValue(code));
        payload.Insert(L"code_verifier", winrt::Windows::Data::Json::JsonValue::CreateStringValue(verifier));
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/sso/token",
            L"",
            payload.Stringify());
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetMeAsync(
        winrt::hstring const& bearerSession)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/auth/me",
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::LogoutAsync(
        winrt::hstring const& bearerSession)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/auth/logout",
            bearerSession,
            L"{}");
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::SearchAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& query)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/search?q=" + winrt::Windows::Foundation::Uri::EscapeComponent(query),
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::ResolveUrlAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& sourceUrl)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(sourceUrl));
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/music/resolve",
            bearerSession,
            payload.Stringify());
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::ImportAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& sourceUrl)
    {
        winrt::Windows::Data::Json::JsonObject payload;
        payload.Insert(L"url", winrt::Windows::Data::Json::JsonValue::CreateStringValue(sourceUrl));
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/music/import",
            bearerSession,
            payload.Stringify());
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetRelatedAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& sourceUrl)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/related?sourceUrl=" + winrt::Windows::Foundation::Uri::EscapeComponent(sourceUrl),
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetLyricsAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& artist,
        winrt::hstring const& title,
        winrt::hstring const& album,
        std::int64_t durationMs,
        winrt::hstring const& sourceUrl)
    {
        std::wstring path = L"/v1/music/lyrics?artist=";
        path += winrt::Windows::Foundation::Uri::EscapeComponent(artist).c_str();
        path += L"&title=";
        path += winrt::Windows::Foundation::Uri::EscapeComponent(title).c_str();
        if (!album.empty())
        {
            path += L"&album=";
            path += winrt::Windows::Foundation::Uri::EscapeComponent(album).c_str();
        }
        if (durationMs > 0)
        {
            path += L"&durationMs=" + std::to_wstring(durationMs);
        }
        if (!sourceUrl.empty())
        {
            path += L"&sourceUrl=";
            path += winrt::Windows::Foundation::Uri::EscapeComponent(sourceUrl).c_str();
        }
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            winrt::hstring{ path },
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetLibraryAsync(
        winrt::hstring const& bearerSession)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/library",
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::SetLikedAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& trackJson,
        bool liked,
        winrt::hstring const& remoteId)
    {
        if (liked)
        {
            co_return co_await SendJsonAsync(
                winrt::Windows::Web::Http::HttpMethod::Post(),
                L"/v1/music/liked",
                bearerSession,
                trackJson);
        }
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Delete(),
            L"/v1/music/liked?id=" + winrt::Windows::Foundation::Uri::EscapeComponent(remoteId),
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::CreatePlaylistAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& playlistJson)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/music/playlists",
            bearerSession,
            playlistJson);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::UpdatePlaylistAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& playlistId,
        winrt::hstring const& playlistJson)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod{ L"PATCH" },
            L"/v1/music/playlists/" + winrt::Windows::Foundation::Uri::EscapeComponent(playlistId),
            bearerSession,
            playlistJson);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::DeletePlaylistAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& playlistId)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Delete(),
            L"/v1/music/playlists/" + winrt::Windows::Foundation::Uri::EscapeComponent(playlistId),
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::AddPlaylistTrackAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& playlistId,
        winrt::hstring const& trackJson)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/music/playlists/" + winrt::Windows::Foundation::Uri::EscapeComponent(playlistId) + L"/tracks",
            bearerSession,
            trackJson);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::PostHistoryBatchAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& batchJson)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Post(),
            L"/v1/music/history/batch",
            bearerSession,
            batchJson);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetCatalogStorefrontsAsync(
        winrt::hstring const& bearerSession)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/catalog/storefronts",
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetCatalogDiscoveryAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& storefront)
    {
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/catalog/discovery?storefront="
                + winrt::Windows::Foundation::Uri::EscapeComponent(NormalizeStorefront(storefront)),
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetCatalogChartAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& storefront,
        winrt::hstring const& type,
        std::int32_t limit,
        std::int32_t offset)
    {
        // The service clamps these too, but sending a sane page keeps a
        // mistyped caller from asking for an enormous response.
        auto safeLimit = std::clamp(limit, 1, 100);
        auto safeOffset = std::clamp(offset, 0, 10000);
        std::wstring path{ L"/v1/music/catalog/charts?storefront=" };
        path += winrt::Windows::Foundation::Uri::EscapeComponent(NormalizeStorefront(storefront)).c_str();
        path += L"&type=";
        path += winrt::Windows::Foundation::Uri::EscapeComponent(NormalizeChartType(type)).c_str();
        path += L"&limit=" + std::to_wstring(safeLimit);
        path += L"&offset=" + std::to_wstring(safeOffset);
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            winrt::hstring{ path },
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> AccountClient::GetCatalogResourceAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& kind,
        winrt::hstring const& id,
        winrt::hstring const& storefront)
    {
        // The kind becomes a path segment, so it is matched against a fixed set
        // rather than escaped and hoped for.
        if (kind != L"albums" && kind != L"playlists")
        {
            throw winrt::hresult_invalid_argument(L"Catalog resource kind is invalid.");
        }
        if (id.empty())
        {
            throw winrt::hresult_invalid_argument(L"Catalog resource id is required.");
        }
        std::wstring path{ L"/v1/music/catalog/" };
        path += kind.c_str();
        path += L"/";
        path += winrt::Windows::Foundation::Uri::EscapeComponent(id).c_str();
        path += L"?storefront=";
        path += winrt::Windows::Foundation::Uri::EscapeComponent(NormalizeStorefront(storefront)).c_str();
        co_return co_await SendJsonAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            winrt::hstring{ path },
            bearerSession);
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Windows::Storage::Streams::IBuffer> AccountClient::GetArtworkAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& artworkUrl)
    {
        auto response = co_await SendAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/artwork?url=" + winrt::Windows::Foundation::Uri::EscapeComponent(artworkUrl),
            bearerSession,
            L"");
        EnsureAccountSuccess(response);
        auto contentLength = response.Content().Headers().ContentLength();
        if (contentLength && contentLength.Value() > MaxArtworkBytes)
        {
            throw winrt::hresult_error(E_FAIL, L"Account artwork is too large.");
        }
        auto buffer = co_await response.Content().ReadAsBufferAsync();
        if (buffer.Length() > MaxArtworkBytes)
        {
            throw winrt::hresult_error(E_FAIL, L"Account artwork is too large.");
        }
        co_return buffer;
    }

    winrt::Windows::Foundation::IAsyncOperation<std::uint32_t> AccountClient::ProbeStreamAsync(
        winrt::hstring const& bearerSession,
        winrt::hstring const& streamUrl)
    {
        auto response = co_await SendAsync(
            winrt::Windows::Web::Http::HttpMethod::Get(),
            L"/v1/music/stream?url=" + winrt::Windows::Foundation::Uri::EscapeComponent(streamUrl),
            bearerSession,
            L"",
            winrt::Windows::Web::Http::HttpCompletionOption::ResponseHeadersRead);
        co_return static_cast<std::uint32_t>(response.StatusCode());
    }
}
