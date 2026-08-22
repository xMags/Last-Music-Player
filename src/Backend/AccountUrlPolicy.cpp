#include "pch.h"
#include "Backend/AccountUrlPolicy.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        struct TrustedOrigin
        {
            winrt::hstring Normalized;
            std::wstring Scheme;
            std::wstring Host;
            std::int32_t Port{};
        };

        std::wstring Lowercase(winrt::hstring const& value)
        {
            std::wstring lowered{ value.c_str() };
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return lowered;
        }

        bool IsLoopbackHost(winrt::hstring const& host) noexcept
        {
            auto lowered = Lowercase(host);
            return lowered == L"127.0.0.1"
                || lowered == L"::1"
                || lowered == L"localhost";
        }

        std::int32_t EffectivePort(
            winrt::Windows::Foundation::Uri const& uri) noexcept
        {
            auto port = uri.Port();
            if (port > 0)
            {
                return port;
            }
            auto scheme = Lowercase(uri.SchemeName());
            if (scheme == L"https")
            {
                return 443;
            }
            if (scheme == L"http")
            {
                return 80;
            }
            return port;
        }

        bool IsAllowedUrlScheme(
            winrt::Windows::Foundation::Uri const& uri) noexcept
        {
            auto scheme = Lowercase(uri.SchemeName());
            if (scheme == L"https")
            {
                return true;
            }
#ifdef _DEBUG
            return scheme == L"http" && IsLoopbackHost(uri.Host());
#else
            return false;
#endif
        }

        bool HasSensitiveQueryField(winrt::hstring const& query)
        {
            static constexpr std::array<std::wstring_view, 17> sensitiveNames{
                L"access_token", L"api_key", L"apikey", L"authorization",
                L"bearer", L"password", L"refresh_token", L"session",
                L"jwt", L"token", L"media_token", L"signature", L"sig",
                L"credential", L"expires", L"auth", L"key"
            };

            std::wstring text{ query.c_str() };
            while (!text.empty() && text.front() == L'?')
            {
                text.erase(text.begin());
            }

            std::size_t start{};
            while (start <= text.size())
            {
                auto end = text.find(L'&', start);
                auto field = text.substr(
                    start,
                    end == std::wstring::npos
                        ? std::wstring::npos
                        : end - start);
                auto equals = field.find(L'=');
                auto encodedName = field.substr(0, equals);
                auto name = Lowercase(
                    winrt::Windows::Foundation::Uri::UnescapeComponent(
                        encodedName));
                if (std::find(
                        sensitiveNames.begin(),
                        sensitiveNames.end(),
                        name) != sensitiveNames.end()
                    || name.rfind(L"x-amz-", 0) == 0
                    || name.rfind(L"x-goog-", 0) == 0)
                {
                    return true;
                }
                if (end == std::wstring::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return false;
        }

        std::optional<TrustedOrigin> ParseTrustedOrigin(
            winrt::hstring const& origin)
        {
            if (origin.empty())
            {
                return std::nullopt;
            }

            winrt::Windows::Foundation::Uri uri{ origin };
            if (!uri.Query().empty()
                || !uri.Fragment().empty()
                || !uri.UserName().empty()
                || !uri.Password().empty())
            {
                return std::nullopt;
            }

            auto path = uri.Path();
            if (!path.empty() && path != L"/")
            {
                return std::nullopt;
            }

            auto scheme = Lowercase(uri.SchemeName());
            auto host = Lowercase(uri.Host());
            if (host.empty())
            {
                return std::nullopt;
            }

            if (!IsAllowedUrlScheme(uri))
            {
                return std::nullopt;
            }

            std::wstring normalized{ origin.c_str() };
            while (!normalized.empty() && normalized.back() == L'/')
            {
                normalized.pop_back();
            }
            if (normalized.empty())
            {
                return std::nullopt;
            }

            return TrustedOrigin{
                winrt::hstring{ normalized },
                std::move(scheme),
                std::move(host),
                EffectivePort(uri)
            };
        }
    }

    bool IsTrustedAccountOrigin(winrt::hstring const& origin) noexcept
    {
        try
        {
            return ParseTrustedOrigin(origin).has_value();
        }
        catch (...)
        {
            return false;
        }
    }

    winrt::hstring NormalizeTrustedAccountOrigin(
        winrt::hstring const& origin) noexcept
    {
        try
        {
            auto parsed = ParseTrustedOrigin(origin);
            return parsed ? parsed->Normalized : winrt::hstring{};
        }
        catch (...)
        {
            return {};
        }
    }

    bool IsUrlFromTrustedAccountOrigin(
        winrt::hstring const& url,
        winrt::hstring const& origin) noexcept
    {
        try
        {
            auto trusted = ParseTrustedOrigin(origin);
            if (!trusted || url.empty())
            {
                return false;
            }

            winrt::Windows::Foundation::Uri candidate{ url };
            if (!candidate.UserName().empty() || !candidate.Password().empty())
            {
                return false;
            }

            return Lowercase(candidate.SchemeName()) == trusted->Scheme
                && Lowercase(candidate.Host()) == trusted->Host
                && EffectivePort(candidate) == trusted->Port;
        }
        catch (...)
        {
            return false;
        }
    }

    bool IsInlineProfileImageData(winrt::hstring const& value) noexcept
    {
        constexpr std::wstring_view scheme{ L"data:" };
        if (value.size() < scheme.size())
        {
            return false;
        }
        for (std::size_t index{}; index < scheme.size(); ++index)
        {
            if (std::towlower(value[static_cast<uint32_t>(index)]) != scheme[index])
            {
                return false;
            }
        }
        return true;
    }

    bool IsSafeInlineProfileImage(winrt::hstring const& value) noexcept
    {
        try
        {
            // Large enough for a real profile photo at a sensible resolution,
            // small enough that a hostile response cannot make the app hold
            // an unbounded string or store one per account.
            constexpr std::size_t MaxInlineImageCharacters = 1'500'000;
            constexpr std::wstring_view scheme{ L"data:" };
            constexpr std::wstring_view encoding{ L";base64," };
            static constexpr std::array<std::wstring_view, 5> allowedMediaTypes{
                L"image/jpeg", L"image/png", L"image/gif", L"image/webp", L"image/bmp"
            };

            std::wstring text{ value.c_str() };
            if (text.empty() || text.size() > MaxInlineImageCharacters)
            {
                return false;
            }

            auto separator = text.find(L',');
            if (separator == std::wstring::npos)
            {
                return false;
            }

            // Only the header is case-folded. The payload past the comma is
            // base64, where case carries data.
            std::wstring header = text.substr(0, separator + 1);
            std::transform(header.begin(), header.end(), header.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            if (!header.starts_with(scheme) || !header.ends_with(encoding))
            {
                return false;
            }

            std::wstring_view mediaType{ header };
            mediaType.remove_prefix(scheme.size());
            mediaType.remove_suffix(encoding.size());
            if (std::find(allowedMediaTypes.begin(), allowedMediaTypes.end(), mediaType)
                == allowedMediaTypes.end())
            {
                return false;
            }

            std::wstring_view payload{ text };
            payload.remove_prefix(separator + 1);
            if (payload.empty() || payload.size() % 4 != 0)
            {
                return false;
            }

            // Validated here rather than left to the decoder, so a malformed
            // payload is rejected at the boundary instead of failing later in
            // the UI or being persisted and replayed on every launch.
            auto dataLength = payload.size();
            while (dataLength > 0 && payload[dataLength - 1] == L'=')
            {
                --dataLength;
            }
            if (dataLength == 0 || payload.size() - dataLength > 2)
            {
                return false;
            }
            for (std::size_t index{}; index < dataLength; ++index)
            {
                auto character = payload[index];
                auto allowed = (character >= L'A' && character <= L'Z')
                    || (character >= L'a' && character <= L'z')
                    || (character >= L'0' && character <= L'9')
                    || character == L'+'
                    || character == L'/';
                if (!allowed)
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

    bool IsSafeAccountProfileUrl(winrt::hstring const& url) noexcept
    {
        try
        {
            constexpr std::size_t MaxProfileUrlCharacters = 2048;
            if (url.empty() || url.size() > MaxProfileUrlCharacters)
            {
                return false;
            }

            winrt::Windows::Foundation::Uri uri{ url };
            return IsAllowedUrlScheme(uri)
                && !uri.Host().empty()
                && uri.UserName().empty()
                && uri.Password().empty()
                && uri.Fragment().empty()
                && !HasSensitiveQueryField(uri.Query());
        }
        catch (...)
        {
            return false;
        }
    }
}
