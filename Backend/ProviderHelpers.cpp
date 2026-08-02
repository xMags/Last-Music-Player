#include "pch.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <initializer_list>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring Lowercase(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
            {
                return static_cast<wchar_t>(std::towlower(ch));
            });
            return value;
        }

        std::wstring ToLowerCopy(winrt::hstring const& value)
        {
            return Lowercase(std::wstring{ value.c_str() });
        }

        bool IsHttpUrl(winrt::hstring const& value)
        {
            auto lower = ToLowerCopy(value);
            return lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0;
        }

        bool IsLoopbackHost(winrt::hstring const& host) noexcept
        {
            auto lowered = ToLowerCopy(host);
            return lowered == L"127.0.0.1" || lowered == L"::1" || lowered == L"localhost";
        }

        winrt::hstring EscapeUrlComponent(winrt::hstring const& value)
        {
            return winrt::Windows::Foundation::Uri::EscapeComponent(value);
        }

        uint64_t StableFnv1a64(std::wstring const& value)
        {
            uint64_t hash = 1469598103934665603ull;
            for (wchar_t ch : value)
            {
                auto code = static_cast<uint32_t>(ch);
                for (int shift = 0; shift < 16; shift += 8)
                {
                    hash ^= static_cast<uint8_t>((code >> shift) & 0xFF);
                    hash *= 1099511628211ull;
                }
            }
            return hash;
        }

        std::wstring Hex64(uint64_t value)
        {
            wchar_t buffer[17]{};
            swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(value));
            return buffer;
        }

        winrt::hstring StableStreamId(
            winrt::hstring const& streamProvider,
            winrt::hstring const& sourceUrl)
        {
            auto idSeed = std::wstring{ sourceUrl.c_str() };
            return streamProvider + L":" + winrt::hstring{ Hex64(StableFnv1a64(idSeed)) };
        }

        winrt::hstring RemoveFragment(winrt::hstring const& url)
        {
            std::wstring text{ url.c_str() };
            auto fragment = text.find(L'#');
            if (fragment != std::wstring::npos)
            {
                text.resize(fragment);
            }
            return winrt::hstring{ text };
        }

        winrt::hstring StripQueryParams(
            winrt::hstring const& url,
            std::initializer_list<wchar_t const*> names)
        {
            std::wstring text{ url.c_str() };
            auto queryStart = text.find(L'?');
            if (queryStart == std::wstring::npos)
            {
                return url;
            }

            std::vector<std::wstring> loweredNames;
            loweredNames.reserve(names.size());
            for (auto name : names)
            {
                loweredNames.push_back(Lowercase(std::wstring{ name }));
            }

            auto fragmentStart = text.find(L'#', queryStart + 1);
            auto queryEnd = fragmentStart == std::wstring::npos ? text.size() : fragmentStart;
            auto query = text.substr(queryStart + 1, queryEnd - queryStart - 1);
            std::vector<std::wstring> kept;
            size_t start = 0;
            while (start <= query.size())
            {
                auto amp = query.find(L'&', start);
                auto part = query.substr(start, amp == std::wstring::npos ? std::wstring::npos : amp - start);
                auto equals = part.find(L'=');
                auto parameterName = Lowercase(part.substr(0, equals));
                auto drop = std::find(loweredNames.begin(), loweredNames.end(), parameterName) != loweredNames.end();
                if (!drop && !part.empty())
                {
                    kept.push_back(part);
                }
                if (amp == std::wstring::npos)
                {
                    break;
                }
                start = amp + 1;
            }

            std::wstring rebuilt = text.substr(0, queryStart);
            if (!kept.empty())
            {
                rebuilt += L"?";
                for (size_t i = 0; i < kept.size(); ++i)
                {
                    if (i > 0)
                    {
                        rebuilt += L"&";
                    }
                    rebuilt += kept[i];
                }
            }
            if (fragmentStart != std::wstring::npos)
            {
                rebuilt += text.substr(fragmentStart);
            }
            return winrt::hstring{ rebuilt };
        }

        bool ContainsNamedField(
            winrt::hstring const& component,
            std::initializer_list<wchar_t const*> exactNames,
            std::initializer_list<wchar_t const*> namePrefixes = {})
        {
            auto text = ToLowerCopy(component);
            while (!text.empty() && (text.front() == L'?' || text.front() == L'#'))
            {
                text.erase(text.begin());
            }

            size_t start = 0;
            while (start <= text.size())
            {
                auto amp = text.find(L'&', start);
                auto part = text.substr(start, amp == std::wstring::npos ? std::wstring::npos : amp - start);
                auto equals = part.find(L'=');
                auto name = part.substr(0, equals);

                for (auto expected : exactNames)
                {
                    if (name == expected)
                    {
                        return true;
                    }
                }
                for (auto prefix : namePrefixes)
                {
                    if (name.rfind(prefix, 0) == 0)
                    {
                        return true;
                    }
                }

                if (amp == std::wstring::npos)
                {
                    break;
                }
                start = amp + 1;
            }
            return false;
        }

        std::wstring QueryParameter(winrt::hstring const& url, std::wstring const& expectedName)
        {
            std::wstring text{ url.c_str() };
            auto queryStart = text.find(L'?');
            if (queryStart == std::wstring::npos)
            {
                return {};
            }

            auto expected = Lowercase(expectedName);
            auto fragmentStart = text.find(L'#', queryStart + 1);
            auto queryEnd = fragmentStart == std::wstring::npos ? text.size() : fragmentStart;
            auto query = text.substr(queryStart + 1, queryEnd - queryStart - 1);
            size_t start = 0;
            while (start <= query.size())
            {
                auto amp = query.find(L'&', start);
                auto part = query.substr(start, amp == std::wstring::npos ? std::wstring::npos : amp - start);
                auto equals = part.find(L'=');
                auto name = Lowercase(part.substr(0, equals));
                if (name == expected)
                {
                    return equals == std::wstring::npos ? std::wstring{} : part.substr(equals + 1);
                }
                if (amp == std::wstring::npos)
                {
                    break;
                }
                start = amp + 1;
            }
            return {};
        }

        winrt::hstring AppendAccessToken(
            winrt::hstring const& url,
            winrt::hstring const& apiToken)
        {
            if (apiToken.empty())
            {
                return url;
            }

            std::wstring text{ url.c_str() };
            std::wstring fragment;
            if (auto fragmentStart = text.find(L'#'); fragmentStart != std::wstring::npos)
            {
                fragment = text.substr(fragmentStart);
                text.resize(fragmentStart);
            }
            wchar_t separator = text.find(L'?') == std::wstring::npos ? L'?' : L'&';
            text += separator;
            text += L"access_token=";
            text += EscapeUrlComponent(apiToken).c_str();
            text += fragment;
            return winrt::hstring{ text };
        }

        winrt::hstring DetermineStreamProvider(winrt::hstring const& provider)
        {
            auto normalized = ToLowerCopy(provider);
            return normalized.empty() || normalized == L"direct"
                ? winrt::hstring{ L"direct" }
                : winrt::hstring{ L"remote" };
        }

        winrt::hstring RebaseProviderStreamUrl(
            winrt::hstring const& streamUrl,
            winrt::hstring const& providerBaseUrl,
            winrt::hstring const& apiToken)
        {
            if (!IsHttpUrl(streamUrl))
            {
                return {};
            }

            std::wstring text{ streamUrl.c_str() };
            if (auto fragmentStart = text.find(L"#lmp="); fragmentStart != std::wstring::npos)
            {
                text.resize(fragmentStart);
            }
            auto lowered = ToLowerCopy(winrt::hstring{ text });
            auto pathIndex = lowered.find(L"/v1/stream/");
            if (pathIndex == std::wstring::npos)
            {
                return {};
            }

            std::wstring rebuilt{ NormalizeProviderBaseUrl(providerBaseUrl).c_str() };
            rebuilt += text.substr(pathIndex);
            auto cleaned = StripQueryParams(
                winrt::hstring{ rebuilt }, { L"media_token", L"access_token" });
            return AppendAccessToken(cleaned, apiToken);
        }

        bool IsCurrentProviderPath(
            winrt::hstring const& url,
            winrt::hstring const& providerBaseUrl,
            wchar_t const* path)
        {
            auto loweredUrl = ToLowerCopy(RemoveFragment(url));
            auto loweredBase = ToLowerCopy(NormalizeProviderBaseUrl(providerBaseUrl));
            std::wstring prefix = loweredBase;
            prefix += path;
            return loweredUrl.rfind(prefix, 0) == 0;
        }
    }

    bool IsSafeRemoteUrl(winrt::hstring const& url, RemoteUrlUse use) noexcept
    {
        try
        {
            if (url.empty())
            {
                return false;
            }

            winrt::Windows::Foundation::Uri uri{ url };
            auto scheme = ToLowerCopy(uri.SchemeName());
            if (scheme != L"https" && !(scheme == L"http" && IsLoopbackHost(uri.Host())))
            {
                return false;
            }
            if (!uri.UserName().empty() || !uri.Password().empty())
            {
                return false;
            }

            auto containsCredential = [](winrt::hstring const& component)
            {
                return ContainsNamedField(component,
                    { L"access_token", L"api_key", L"apikey", L"authorization", L"bearer",
                      L"password", L"refresh_token", L"session", L"jwt" });
            };
            if (containsCredential(uri.Query()) || containsCredential(uri.Fragment()))
            {
                return false;
            }

            if (use == RemoteUrlUse::Durable)
            {
                auto containsSignedDelivery = [](winrt::hstring const& component)
                {
                    return ContainsNamedField(component,
                        { L"token", L"media_token", L"signature", L"sig", L"credential",
                          L"expires", L"auth", L"key" },
                        { L"x-amz-", L"x-goog-" });
                };
                if (containsSignedDelivery(uri.Query()) || containsSignedDelivery(uri.Fragment()))
                {
                    return false;
                }
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool IsSyncableRemoteSource(
        winrt::hstring const& sourceKind,
        winrt::hstring const& sourceUrl) noexcept
    {
        try
        {
            if (ToLowerCopy(sourceKind) == L"local")
            {
                return false;
            }
            return IsSafeRemoteUrl(sourceUrl, RemoteUrlUse::Durable);
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::hstring NormalizeProviderBaseUrl(winrt::hstring const& savedBase)
    {
        std::wstring base{
            (savedBase.empty() ? winrt::hstring{ L"http://127.0.0.1:4527" } : savedBase).c_str()
        };
        while (!base.empty() && (base.back() == L'/' || base.back() == L'\\'))
        {
            base.pop_back();
        }
        return winrt::hstring{ base };
    }

    winrt::hstring RemoveLegacyProviderUrlCredential(winrt::hstring const& url)
    {
        if (!IsHttpUrl(url))
        {
            return url;
        }
        auto lowered = ToLowerCopy(url);
        if (lowered.find(L"/v1/stream/") == std::wstring::npos
            && lowered.find(L"/v1/artwork") == std::wstring::npos)
        {
            return url;
        }
        return StripQueryParams(url, { L"access_token" });
    }

    winrt::hstring BuildProviderStreamUrl(
        winrt::hstring const& filePath,
        winrt::hstring const& sourceUrl,
        winrt::hstring const& provider,
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl,
        winrt::hstring const& apiToken)
    {
        (void)artworkUrl;

        if (auto providerStreamUrl = RebaseProviderStreamUrl(filePath, providerBaseUrl, apiToken);
            !providerStreamUrl.empty())
        {
            return providerStreamUrl;
        }

        if (IsHttpUrl(sourceUrl))
        {
            auto streamProvider = DetermineStreamProvider(provider);
            auto streamId = StableStreamId(streamProvider, sourceUrl);
            std::wstring streamUrl{ NormalizeProviderBaseUrl(providerBaseUrl).c_str() };
            streamUrl += L"/v1/stream/";
            streamUrl += EscapeUrlComponent(streamId).c_str();
            streamUrl += L"?url=";
            streamUrl += EscapeUrlComponent(sourceUrl).c_str();
            return AppendAccessToken(winrt::hstring{ streamUrl }, apiToken);
        }

        if (IsHttpUrl(filePath) && ToLowerCopy(filePath).find(L"/v1/stream/") != std::wstring::npos)
        {
            auto cleaned = StripQueryParams(filePath, { L"media_token", L"access_token" });
            return AppendAccessToken(cleaned, apiToken);
        }
        return {};
    }

    winrt::hstring BuildProviderStreamUrl(
        winrt::hstring const& filePath,
        winrt::hstring const& providerBaseUrl)
    {
        if (!IsHttpUrl(filePath)
            || !IsCurrentProviderPath(filePath, providerBaseUrl, L"/v1/stream/"))
        {
            return {};
        }
        return RemoveFragment(RemoveLegacyProviderUrlCredential(filePath));
    }

    winrt::hstring BuildProviderArtworkUrl(
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl,
        winrt::hstring const& apiToken)
    {
        if (!IsHttpUrl(artworkUrl))
        {
            return artworkUrl;
        }

        std::wstring text{ artworkUrl.c_str() };
        auto pathIndex = ToLowerCopy(artworkUrl).find(L"/v1/artwork");
        if (pathIndex == std::wstring::npos)
        {
            return artworkUrl;
        }

        std::wstring rebuilt{ NormalizeProviderBaseUrl(providerBaseUrl).c_str() };
        rebuilt += text.substr(pathIndex);
        auto cleaned = StripQueryParams(
            winrt::hstring{ rebuilt }, { L"media_token", L"access_token" });
        return AppendAccessToken(cleaned, apiToken);
    }

    winrt::hstring BuildProviderArtworkUrl(
        winrt::hstring const& artworkUrl,
        winrt::hstring const& providerBaseUrl)
    {
        if (!IsHttpUrl(artworkUrl))
        {
            return artworkUrl;
        }

        auto lowered = ToLowerCopy(artworkUrl);
        if (lowered.find(L"/v1/artwork") == std::wstring::npos)
        {
            return artworkUrl;
        }
        if (!IsCurrentProviderPath(artworkUrl, providerBaseUrl, L"/v1/artwork"))
        {
            return {};
        }
        return RemoveFragment(RemoveLegacyProviderUrlCredential(artworkUrl));
    }

    bool ProviderMediaUrlNeedsRefresh(
        winrt::hstring const& mediaUrl,
        winrt::hstring const& expectedScope,
        bool signedTokenRequired)
    {
        if (!signedTokenRequired || mediaUrl.empty())
        {
            return false;
        }

        auto scope = ToLowerCopy(expectedScope);
        if (scope != L"stream" && scope != L"artwork")
        {
            return true;
        }

        auto expectedPath = scope == L"stream" ? L"/v1/stream/" : L"/v1/artwork";
        if (ToLowerCopy(mediaUrl).find(expectedPath) == std::wstring::npos)
        {
            return false;
        }

        auto token = QueryParameter(mediaUrl, L"media_token");
        auto firstDot = token.find(L'.');
        auto secondDot = firstDot == std::wstring::npos
            ? std::wstring::npos
            : token.find(L'.', firstDot + 1);
        if (firstDot == std::wstring::npos
            || secondDot == std::wstring::npos
            || secondDot + 1 >= token.size()
            || token.substr(0, firstDot) != scope)
        {
            return true;
        }

        try
        {
            auto expiresAtMs = std::stoll(token.substr(firstDot + 1, secondDot - firstDot - 1));
            auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            constexpr long long kRefreshGraceMs = 30'000;
            return expiresAtMs <= nowMs + kRefreshGraceMs;
        }
        catch (...)
        {
            return true;
        }
    }

    bool IsTrustedAccountMediaUrl(
        winrt::hstring const& mediaUrl,
        winrt::hstring const& accountMediaOrigin,
        winrt::hstring const& expectedScope) noexcept
    {
        try
        {
            auto scope = ToLowerCopy(expectedScope);
            if (accountMediaOrigin.empty()
                || (scope != L"stream" && scope != L"artwork")
                || !IsSafeRemoteUrl(mediaUrl, RemoteUrlUse::EphemeralMedia))
            {
                return false;
            }

            auto expectedPath = scope == L"stream" ? L"/v1/stream/" : L"/v1/artwork";
            return IsCurrentProviderPath(mediaUrl, accountMediaOrigin, expectedPath)
                && !ProviderMediaUrlNeedsRefresh(mediaUrl, expectedScope, true);
        }
        catch (...)
        {
            return false;
        }
    }
}
