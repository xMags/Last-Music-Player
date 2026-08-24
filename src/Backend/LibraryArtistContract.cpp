#include "pch.h"
#include "Backend/LibraryArtistContract.h"

#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>

#include <winrt/Windows.Data.Json.h>

namespace LastMusicPlayer::Backend
{
    namespace Json = winrt::Windows::Data::Json;

    namespace
    {
        constexpr std::size_t MaxPublicIdCharacters = 256;
        constexpr std::size_t MaxProviderCharacters = 80;
        constexpr std::size_t MaxCatalogIdCharacters = 512;
        constexpr std::size_t MaxNameCharacters = 512;
        constexpr std::size_t MaxUrlCharacters = 4096;
        constexpr std::size_t MaxCursorCharacters = 2048;

        std::optional<std::wstring> BoundedString(
            Json::JsonObject const& object,
            wchar_t const* key,
            std::size_t maximum,
            bool required = false)
        {
            if (!object || !object.HasKey(key))
            {
                return required ? std::nullopt : std::optional<std::wstring>{ std::wstring{} };
            }
            auto const value = object.GetNamedValue(key);
            if (!required && value.ValueType() == Json::JsonValueType::Null)
            {
                return std::wstring{};
            }
            if (value.ValueType() != Json::JsonValueType::String)
            {
                return std::nullopt;
            }
            std::wstring result{ value.GetString().c_str() };
            if (result.size() > maximum || (required && result.empty()))
            {
                return std::nullopt;
            }
            return result;
        }

        Json::JsonObject ObjectValue(Json::JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return nullptr;
            }
            auto const value = object.GetNamedValue(key);
            return value.ValueType() == Json::JsonValueType::Object
                ? value.GetObject()
                : nullptr;
        }

        std::optional<bool> BooleanValue(Json::JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return std::nullopt;
            }
            auto const value = object.GetNamedValue(key);
            return value.ValueType() == Json::JsonValueType::Boolean
                ? std::optional<bool>{ value.GetBoolean() }
                : std::nullopt;
        }

        std::optional<std::int64_t> CountValue(
            Json::JsonObject const& object,
            wchar_t const* key,
            bool required)
        {
            if (!object || !object.HasKey(key))
            {
                return required ? std::nullopt : std::optional<std::int64_t>{ 0 };
            }
            auto const value = object.GetNamedValue(key);
            if (value.ValueType() != Json::JsonValueType::Number)
            {
                return std::nullopt;
            }
            auto const number = value.GetNumber();
            if (!std::isfinite(number)
                || number < 0.0
                || number > static_cast<double>((std::numeric_limits<std::int64_t>::max)())
                || std::floor(number) != number)
            {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(number);
        }

        bool SafeOptionalUrl(std::wstring const& value)
        {
            return value.empty()
                || (value.size() <= MaxUrlCharacters
                    && IsSafeRemoteUrl(winrt::hstring(value), RemoteUrlUse::Durable));
        }

        std::optional<LibraryArtist> ParseArtist(Json::JsonObject const& object)
        {
            auto id = BoundedString(object, L"id", MaxPublicIdCharacters, true);
            auto name = BoundedString(object, L"name", MaxNameCharacters, true);
            auto provider = BoundedString(object, L"provider", MaxProviderCharacters);
            auto providerCatalogId = BoundedString(
                object, L"providerCatalogId", MaxCatalogIdCharacters);
            auto providerSourceUrl = BoundedString(object, L"providerSourceUrl", MaxUrlCharacters);

            auto const providerIdentity = ObjectValue(object, L"providerIdentity");
            if (providerIdentity)
            {
                auto nestedProvider = BoundedString(
                    providerIdentity, L"provider", MaxProviderCharacters, true);
                auto nestedCatalogId = BoundedString(
                    providerIdentity, L"catalogId", MaxCatalogIdCharacters);
                auto nestedSourceUrl = BoundedString(
                    providerIdentity, L"sourceUrl", MaxUrlCharacters);
                if (!nestedProvider || !nestedCatalogId || !nestedSourceUrl)
                {
                    return std::nullopt;
                }
                provider = std::move(nestedProvider);
                providerCatalogId = std::move(nestedCatalogId);
                providerSourceUrl = std::move(nestedSourceUrl);
            }
            else
            {
                if (providerCatalogId && providerCatalogId->empty())
                {
                    providerCatalogId = BoundedString(object, L"catalogId", MaxCatalogIdCharacters);
                }
                if (providerSourceUrl && providerSourceUrl->empty())
                {
                    providerSourceUrl = BoundedString(object, L"sourceUrl", MaxUrlCharacters);
                }
            }

            auto appleCatalogId = BoundedString(object, L"appleCatalogId", MaxCatalogIdCharacters);
            auto artworkUrl = BoundedString(object, L"artworkUrl", MaxUrlCharacters);
            auto heroArtworkUrl = BoundedString(object, L"heroArtworkUrl", MaxUrlCharacters);
            auto trackCount = CountValue(object, L"trackCount", true);
            auto albumCount = CountValue(object, L"albumCount", true);
            auto playCount = CountValue(object, L"playCount", true);
            auto followed = BooleanValue(object, L"followed");
            auto resolving = BooleanValue(object, L"resolving");
            if (!id || !name || !provider || provider->empty()
                || !providerCatalogId || !providerSourceUrl || !appleCatalogId
                || !artworkUrl || !heroArtworkUrl || !trackCount || !albumCount
                || !playCount || !followed || !resolving
                || !IsValidLibraryArtistPublicId(*id)
                || !SafeOptionalUrl(*providerSourceUrl)
                || !SafeOptionalUrl(*artworkUrl)
                || !SafeOptionalUrl(*heroArtworkUrl))
            {
                return std::nullopt;
            }

            LibraryArtistIdentity detailIdentity{
                *providerCatalogId,
                *providerSourceUrl,
                *name,
                *provider
            };
            if (auto const detail = ObjectValue(object, L"detailIdentity"))
            {
                auto catalogId = BoundedString(detail, L"catalogId", MaxCatalogIdCharacters);
                auto sourceUrl = BoundedString(detail, L"sourceUrl", MaxUrlCharacters);
                auto detailName = BoundedString(detail, L"name", MaxNameCharacters);
                auto detailProvider = BoundedString(detail, L"provider", MaxProviderCharacters);
                if (!catalogId || !sourceUrl || !detailName || !detailProvider
                    || !SafeOptionalUrl(*sourceUrl))
                {
                    return std::nullopt;
                }
                if (!catalogId->empty()) detailIdentity.CatalogId = std::move(*catalogId);
                if (!sourceUrl->empty()) detailIdentity.SourceUrl = std::move(*sourceUrl);
                if (!detailName->empty()) detailIdentity.Name = std::move(*detailName);
                if (!detailProvider->empty()) detailIdentity.Provider = std::move(*detailProvider);
            }
            return LibraryArtist{
                std::move(*id),
                std::move(*name),
                std::move(*provider),
                std::move(*providerCatalogId),
                std::move(*providerSourceUrl),
                std::move(*appleCatalogId),
                std::move(*artworkUrl),
                std::move(*heroArtworkUrl),
                *trackCount,
                *albumCount,
                *playCount,
                *followed,
                *resolving,
                std::move(detailIdentity)
            };
        }

        std::wstring Trimmed(std::wstring value)
        {
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t value)
            {
                return !std::iswspace(value);
            }));
            value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t value)
            {
                return !std::iswspace(value);
            }).base(), value.end());
            return value;
        }
    }

    wchar_t const* LibraryArtistViewName(LibraryArtistView view) noexcept
    {
        switch (view)
        {
        case LibraryArtistView::Followed: return L"followed";
        case LibraryArtistView::All: return L"all";
        default: return L"default";
        }
    }

    bool IsValidLibraryArtistPublicId(std::wstring const& id) noexcept
    {
        if (id.empty() || id.size() > MaxPublicIdCharacters)
        {
            return false;
        }
        return std::none_of(id.begin(), id.end(), [](wchar_t value)
        {
            return value < L' ' || value == 0x7f;
        });
    }

    std::optional<winrt::hstring> BuildLibraryArtistListPath(
        LibraryArtistListRequest const& request) noexcept
    {
        try
        {
            auto query = Trimmed(request.Query);
            if (query.size() > MaxLibraryArtistSearchCharacters
                || request.Limit == 0
                || request.Limit > MaxLibraryArtistPageSize
                || request.Cursor.size() > MaxCursorCharacters)
            {
                return std::nullopt;
            }

            std::wstring path{ L"/v1/music/library/artists?view=" };
            path += LibraryArtistViewName(request.View);
            path += L"&limit=" + std::to_wstring(request.Limit);
            if (!query.empty())
            {
                path += L"&q=";
                path += winrt::Windows::Foundation::Uri::EscapeComponent(winrt::hstring(query)).c_str();
            }
            if (!request.Cursor.empty())
            {
                path += L"&cursor=";
                path += winrt::Windows::Foundation::Uri::EscapeComponent(
                    winrt::hstring(request.Cursor)).c_str();
            }
            return winrt::hstring(path);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    ParsedLibraryArtistPage ParseLibraryArtistPage(winrt::hstring const& payload) noexcept
    {
        try
        {
            Json::JsonObject root;
            if (!Json::JsonObject::TryParse(payload, root) || !root)
            {
                return {};
            }
            if (auto const data = ObjectValue(root, L"data"))
            {
                root = data;
            }
            if (!root.HasKey(L"artists"))
            {
                return {};
            }
            auto const artistsValue = root.GetNamedValue(L"artists");
            if (artistsValue.ValueType() != Json::JsonValueType::Array)
            {
                return {};
            }
            auto const artists = artistsValue.GetArray();
            if (artists.Size() > MaxLibraryArtistPageSize)
            {
                return {};
            }

            ParsedLibraryArtistPage page;
            page.Artists.reserve(artists.Size());
            for (std::uint32_t index = 0; index < artists.Size(); ++index)
            {
                auto const value = artists.GetAt(index);
                if (value.ValueType() != Json::JsonValueType::Object)
                {
                    return {};
                }
                auto parsed = ParseArtist(value.GetObject());
                if (!parsed)
                {
                    return {};
                }
                page.Artists.push_back(std::move(*parsed));
            }

            auto cursor = BoundedString(root, L"nextCursor", MaxCursorCharacters);
            if (!cursor)
            {
                return {};
            }
            page.NextCursor = std::move(*cursor);
            if (root.HasKey(L"totalCount"))
            {
                auto total = CountValue(root, L"totalCount", true);
                if (!total)
                {
                    return {};
                }
                page.TotalCount = *total;
            }
            page.Valid = true;
            return page;
        }
        catch (...)
        {
            return {};
        }
    }

    std::optional<std::vector<AccountLibraryTrackDto>> ParseLibraryArtistTracks(
        winrt::hstring const& payload) noexcept
    {
        auto parsed = ParseAccountLibrary(payload);
        if (!parsed.Valid)
        {
            return std::nullopt;
        }
        return std::move(parsed.Tracks);
    }
}
