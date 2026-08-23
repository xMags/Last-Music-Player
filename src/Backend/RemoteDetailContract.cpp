#include "pch.h"
#include "Backend/RemoteDetailContract.h"

#include "Backend/ProviderHelpers.h"

#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        using winrt::Windows::Data::Json::JsonArray;
        using winrt::Windows::Data::Json::JsonObject;
        using winrt::Windows::Data::Json::JsonValueType;

        constexpr std::size_t MaxPayloadCharacters = 1'000'000;
        constexpr std::size_t MaxIdentifierCharacters = 256;
        constexpr std::size_t MaxNameCharacters = 256;
        constexpr std::size_t MaxProviderCharacters = 64;
        constexpr std::size_t MaxUrlCharacters = 4096;
        constexpr std::size_t MaxBiographyCharacters = 20'000;
        constexpr std::size_t MaxGenreCharacters = 128;
        constexpr std::uint32_t MaxGenreCount = 32;
        constexpr std::uint32_t MaxTopSongCount = 10;
        constexpr std::uint32_t MaxReleaseCount = 24;
        constexpr std::uint32_t MaxRelatedArtistCount = 12;
        constexpr std::uint32_t MaxAlbumTrackCount = 250;

        bool IsControl(wchar_t character) noexcept
        {
            return (character >= 0 && character < 0x20 && character != L'\n' && character != L'\t')
                || character == 0x7f;
        }

        std::optional<winrt::hstring> BoundedString(
            JsonObject const& object,
            wchar_t const* key,
            std::size_t maximum,
            bool required = false)
        {
            if (!object || !object.HasKey(key))
            {
                return required ? std::nullopt : std::optional<winrt::hstring>{ winrt::hstring{} };
            }
            auto value = object.GetNamedValue(key);
            if (value.ValueType() != JsonValueType::String)
            {
                return std::nullopt;
            }
            auto text = value.GetString();
            if (text.size() > maximum
                || std::any_of(text.begin(), text.end(), IsControl)
                || (required && text.empty()))
            {
                return std::nullopt;
            }
            return text;
        }

        std::optional<winrt::hstring> FirstBoundedString(
            JsonObject const& object,
            std::initializer_list<wchar_t const*> keys,
            std::size_t maximum,
            bool required = false)
        {
            for (auto key : keys)
            {
                if (!object || !object.HasKey(key))
                {
                    continue;
                }
                return BoundedString(object, key, maximum, required);
            }
            return required ? std::nullopt : std::optional<winrt::hstring>{ winrt::hstring{} };
        }

        std::optional<JsonObject> Object(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return std::nullopt;
            }
            auto value = object.GetNamedValue(key);
            if (value.ValueType() != JsonValueType::Object)
            {
                return std::nullopt;
            }
            return value.GetObject();
        }

        std::optional<JsonArray> Array(
            JsonObject const& object,
            wchar_t const* key,
            std::uint32_t maximum)
        {
            if (!object || !object.HasKey(key))
            {
                return JsonArray{};
            }
            auto value = object.GetNamedValue(key);
            if (value.ValueType() != JsonValueType::Array)
            {
                return std::nullopt;
            }
            auto array = value.GetArray();
            return array.Size() <= maximum ? std::optional<JsonArray>{ array } : std::nullopt;
        }

        bool IsDigitAt(std::wstring_view value, std::size_t index) noexcept
        {
            return index < value.size() && value[index] >= L'0' && value[index] <= L'9';
        }

        bool IsRfc3339(winrt::hstring const& value) noexcept
        {
            std::wstring_view text{ value.c_str(), value.size() };
            if (text.size() < 20
                || text[4] != L'-' || text[7] != L'-'
                || (text[10] != L'T' && text[10] != L't')
                || text[13] != L':' || text[16] != L':')
            {
                return false;
            }
            for (auto index : { 0u, 1u, 2u, 3u, 5u, 6u, 8u, 9u, 11u, 12u, 14u, 15u, 17u, 18u })
            {
                if (!IsDigitAt(text, index))
                {
                    return false;
                }
            }
            auto twoDigits = [&](std::size_t index)
            {
                return static_cast<unsigned>((text[index] - L'0') * 10 + (text[index + 1] - L'0'));
            };
            if (twoDigits(5) < 1 || twoDigits(5) > 12
                || twoDigits(8) < 1 || twoDigits(8) > 31
                || twoDigits(11) > 23 || twoDigits(14) > 59 || twoDigits(17) > 60)
            {
                return false;
            }
            auto zone = text.find_first_of(L"Zz+-", 19);
            if (zone == std::wstring_view::npos)
            {
                return false;
            }
            if (zone > 19)
            {
                if (text[19] != L'.' || zone == 20
                    || !std::all_of(text.begin() + 20, text.begin() + zone, [](wchar_t character)
                    {
                        return character >= L'0' && character <= L'9';
                    }))
                {
                    return false;
                }
            }
            if (text[zone] == L'Z' || text[zone] == L'z')
            {
                return zone + 1 == text.size();
            }
            return zone + 6 == text.size()
                && IsDigitAt(text, zone + 1) && IsDigitAt(text, zone + 2)
                && text[zone + 3] == L':'
                && IsDigitAt(text, zone + 4) && IsDigitAt(text, zone + 5)
                && static_cast<unsigned>((text[zone + 1] - L'0') * 10 + (text[zone + 2] - L'0')) <= 23
                && static_cast<unsigned>((text[zone + 4] - L'0') * 10 + (text[zone + 5] - L'0')) <= 59;
        }

        std::optional<winrt::hstring> SafeUrl(
            JsonObject const& object,
            wchar_t const* key,
            RemoteUrlUse use,
            bool required = false)
        {
            auto value = BoundedString(object, key, MaxUrlCharacters, required);
            if (!value || (!value->empty() && !IsSafeRemoteUrl(*value, use)))
            {
                return std::nullopt;
            }
            return value;
        }

        std::optional<RemoteArtistSummary> ParseArtistSummary(JsonObject const& object)
        {
            auto catalogId = BoundedString(object, L"catalogId", MaxIdentifierCharacters);
            auto name = BoundedString(object, L"name", MaxNameCharacters, true);
            auto provider = BoundedString(object, L"provider", MaxProviderCharacters);
            auto sourceUrl = SafeUrl(object, L"sourceUrl", RemoteUrlUse::Durable);
            auto artworkUrl = SafeUrl(object, L"artworkUrl", RemoteUrlUse::EphemeralMedia);
            if (!catalogId || !name || !provider || !sourceUrl || !artworkUrl
                || (catalogId->empty() && sourceUrl->empty()))
            {
                return std::nullopt;
            }
            return RemoteArtistSummary{ *catalogId, *name, *provider, *sourceUrl, *artworkUrl };
        }

        RemoteReleaseKind ParseReleaseKind(winrt::hstring const& value) noexcept
        {
            std::wstring normalized{ value.c_str() };
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            if (normalized == L"album") return RemoteReleaseKind::Album;
            if (normalized == L"single") return RemoteReleaseKind::Single;
            if (normalized == L"ep") return RemoteReleaseKind::Ep;
            return RemoteReleaseKind::Unknown;
        }

        std::optional<CatalogItem> ParseTrack(
            JsonObject const& object,
            winrt::hstring const& fallbackArtist,
            winrt::hstring const& fallbackProvider)
        {
            auto title = FirstBoundedString(object, { L"title", L"name" }, MaxNameCharacters, true);
            auto artist = FirstBoundedString(object, { L"artist", L"artistName" }, MaxNameCharacters);
            auto album = FirstBoundedString(object, { L"album", L"albumName" }, MaxNameCharacters);
            auto provider = BoundedString(object, L"provider", MaxProviderCharacters);
            auto remoteId = BoundedString(object, L"id", MaxIdentifierCharacters);
            auto catalogId = BoundedString(object, L"catalogId", MaxIdentifierCharacters);
            auto albumCatalogId = BoundedString(object, L"albumCatalogId", MaxIdentifierCharacters);
            auto artistCatalogId = BoundedString(object, L"artistCatalogId", MaxIdentifierCharacters);
            auto sourceUrl = SafeUrl(object, L"sourceUrl", RemoteUrlUse::Durable, true);
            auto streamUrl = SafeUrl(object, L"streamUrl", RemoteUrlUse::EphemeralMedia);
            auto artworkUrl = SafeUrl(object, L"artworkUrl", RemoteUrlUse::EphemeralMedia);
            if (!title || !artist || !album || !provider || !remoteId || !catalogId
                || !albumCatalogId || !artistCatalogId || !sourceUrl || !streamUrl || !artworkUrl)
            {
                return std::nullopt;
            }

            std::int64_t durationMs = 0;
            if (object.HasKey(L"durationMs"))
            {
                auto value = object.GetNamedValue(L"durationMs");
                if (value.ValueType() != JsonValueType::Number)
                {
                    return std::nullopt;
                }
                auto number = value.GetNumber();
                constexpr double MaximumTrackDurationMs = 24.0 * 60.0 * 60.0 * 1000.0;
                if (!std::isfinite(number) || number < 0.0 || number > MaximumTrackDurationMs)
                {
                    return std::nullopt;
                }
                durationMs = static_cast<std::int64_t>(number);
            }

            CatalogItem track;
            track.Type = CatalogResourceType::Song;
            track.Title = *title;
            track.Subtitle = artist->empty() ? fallbackArtist : *artist;
            track.AlbumName = *album;
            track.Provider = provider->empty() ? fallbackProvider : *provider;
            track.RemoteId = *remoteId;
            track.CatalogId = *catalogId;
            track.AlbumCatalogId = *albumCatalogId;
            track.ArtistCatalogId = *artistCatalogId;
            track.SourceUrl = *sourceUrl;
            track.StreamUrl = *streamUrl;
            track.ArtworkUrl = *artworkUrl;
            track.DurationMs = durationMs;
            return track;
        }

        std::optional<RemoteReleaseSummary> ParseRelease(JsonObject const& object)
        {
            auto catalogId = BoundedString(object, L"catalogId", MaxIdentifierCharacters);
            auto title = FirstBoundedString(object, { L"name", L"title" }, MaxNameCharacters, true);
            auto artist = FirstBoundedString(object, { L"artistName", L"artist" }, MaxNameCharacters);
            auto provider = BoundedString(object, L"provider", MaxProviderCharacters);
            auto sourceUrl = SafeUrl(object, L"sourceUrl", RemoteUrlUse::Durable);
            auto artworkUrl = SafeUrl(object, L"artworkUrl", RemoteUrlUse::EphemeralMedia);
            auto releaseDate = BoundedString(object, L"releaseDate", 32);
            auto kindText = FirstBoundedString(object, { L"releaseType", L"resourceType" }, 16, true);
            if (!catalogId || !title || !artist || !provider || !sourceUrl
                || !artworkUrl || !releaseDate || !kindText)
            {
                return std::nullopt;
            }
            auto kind = ParseReleaseKind(*kindText);
            if (kind == RemoteReleaseKind::Unknown || (catalogId->empty() && sourceUrl->empty()))
            {
                return std::nullopt;
            }

            CatalogItem release;
            release.Type = CatalogResourceType::Album;
            release.CatalogId = *catalogId;
            release.Title = *title;
            release.Subtitle = *artist;
            release.Provider = *provider;
            release.SourceUrl = *sourceUrl;
            release.ArtworkUrl = *artworkUrl;
            release.ReleaseDate = *releaseDate;
            if (object.HasKey(L"trackCount"))
            {
                auto value = object.GetNamedValue(L"trackCount");
                if (value.ValueType() != JsonValueType::Number
                    || !std::isfinite(value.GetNumber())
                    || value.GetNumber() < 0.0
                    || value.GetNumber() > static_cast<double>((std::numeric_limits<std::int32_t>::max)()))
                {
                    return std::nullopt;
                }
                release.TrackCount = static_cast<std::int32_t>(value.GetNumber());
            }
            return RemoteReleaseSummary{ std::move(release), kind };
        }

        std::optional<std::vector<winrt::hstring>> ParseGenres(JsonObject const& object)
        {
            auto genres = Array(object, L"genreNames", MaxGenreCount);
            if (!genres)
            {
                return std::nullopt;
            }
            std::vector<winrt::hstring> result;
            result.reserve(genres->Size());
            for (std::uint32_t index = 0; index < genres->Size(); ++index)
            {
                auto value = genres->GetAt(index);
                if (value.ValueType() != JsonValueType::String)
                {
                    return std::nullopt;
                }
                auto text = value.GetString();
                if (text.empty() || text.size() > MaxGenreCharacters
                    || std::any_of(text.begin(), text.end(), IsControl))
                {
                    return std::nullopt;
                }
                result.push_back(text);
            }
            return result;
        }

        winrt::hstring PlainText(winrt::hstring const& value)
        {
            std::wstring result;
            result.reserve(value.size());
            bool inTag = false;
            for (auto character : value)
            {
                if (character == L'<')
                {
                    inTag = true;
                    continue;
                }
                if (inTag)
                {
                    if (character == L'>')
                    {
                        inTag = false;
                    }
                    continue;
                }
                if (!IsControl(character))
                {
                    result.push_back(character);
                }
            }
            return winrt::hstring{ result };
        }

        std::optional<JsonObject> ParseRoot(winrt::hstring const& payload)
        {
            if (payload.empty() || payload.size() > MaxPayloadCharacters)
            {
                return std::nullopt;
            }
            JsonObject root{ nullptr };
            if (!JsonObject::TryParse(payload, root))
            {
                return std::nullopt;
            }
            return root;
        }

        std::optional<bool> Boolean(JsonObject const& object, wchar_t const* key)
        {
            if (!object.HasKey(key))
            {
                return false;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Boolean
                ? std::optional<bool>{ value.GetBoolean() }
                : std::nullopt;
        }

        bool ArrayContains(JsonArray const& array, std::wstring_view expected)
        {
            for (std::uint32_t index = 0; index < array.Size(); ++index)
            {
                auto value = array.GetAt(index);
                if (value.ValueType() == JsonValueType::String
                    && std::wstring_view{ value.GetString().c_str(), value.GetString().size() } == expected)
                {
                    return true;
                }
            }
            return false;
        }
    }

    std::optional<winrt::hstring> BuildRemoteDetailQuery(
        RemoteDetailIdentity const& identity) noexcept
    {
        try
        {
            if (!identity.CatalogId.empty())
            {
                if (identity.CatalogId.size() > MaxIdentifierCharacters
                    || std::any_of(identity.CatalogId.begin(), identity.CatalogId.end(), IsControl))
                {
                    return std::nullopt;
                }
                return L"catalogId=" + winrt::Windows::Foundation::Uri::EscapeComponent(identity.CatalogId);
            }
            if (!identity.SourceUrl.empty())
            {
                if (identity.SourceUrl.size() > MaxUrlCharacters
                    || !IsSafeRemoteUrl(identity.SourceUrl, RemoteUrlUse::Durable))
                {
                    return std::nullopt;
                }
                return L"sourceUrl=" + winrt::Windows::Foundation::Uri::EscapeComponent(identity.SourceUrl);
            }
            if (identity.Name.empty() || identity.Name.size() > MaxNameCharacters
                || std::any_of(identity.Name.begin(), identity.Name.end(), IsControl))
            {
                return std::nullopt;
            }
            return L"name=" + winrt::Windows::Foundation::Uri::EscapeComponent(identity.Name);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    bool ProviderSupportsCapability(
        winrt::hstring const& payload,
        std::wstring_view capability) noexcept
    {
        try
        {
            auto root = ParseRoot(payload);
            if (!root || capability.empty())
            {
                return false;
            }
            auto direct = Array(*root, L"capabilities", 128);
            if (!direct)
            {
                return false;
            }
            if (ArrayContains(*direct, capability))
            {
                return true;
            }
            auto providers = Array(*root, L"providers", 64);
            if (!providers)
            {
                return false;
            }
            for (std::uint32_t index = 0; index < providers->Size(); ++index)
            {
                auto value = providers->GetAt(index);
                if (value.ValueType() != JsonValueType::Object)
                {
                    continue;
                }
                auto capabilities = Array(value.GetObject(), L"capabilities", 128);
                if (capabilities && ArrayContains(*capabilities, capability))
                {
                    return true;
                }
            }
            return false;
        }
        catch (...)
        {
            return false;
        }
    }

    std::optional<RemoteArtistDetail> ParseRemoteArtistDetail(
        winrt::hstring const& payload) noexcept
    {
        try
        {
            auto root = ParseRoot(payload);
            auto artistObject = root ? Object(*root, L"artist") : std::nullopt;
            auto artist = artistObject ? ParseArtistSummary(*artistObject) : std::nullopt;
            if (!root || !artistObject || !artist)
            {
                return std::nullopt;
            }
            auto hero = SafeUrl(*artistObject, L"heroArtworkUrl", RemoteUrlUse::EphemeralMedia);
            auto biography = BoundedString(*artistObject, L"biography", MaxBiographyCharacters);
            auto genres = ParseGenres(*artistObject);
            auto topSongs = Array(*root, L"topSongs", MaxTopSongCount);
            auto releases = Array(*root, L"releases", MaxReleaseCount);
            auto relatedArtists = Array(*root, L"relatedArtists", MaxRelatedArtistCount);
            auto stale = Boolean(*root, L"stale");
            auto fetchedAt = BoundedString(*root, L"fetchedAt", 64, true);
            if (!hero || !biography || !genres || !topSongs || !releases
                || !relatedArtists || !stale || !fetchedAt || !IsRfc3339(*fetchedAt))
            {
                return std::nullopt;
            }

            RemoteArtistDetail result;
            result.Artist = *artist;
            result.HeroArtworkUrl = *hero;
            result.Biography = PlainText(*biography);
            result.GenreNames = std::move(*genres);
            result.Stale = *stale;
            result.FetchedAt = *fetchedAt;

            for (std::uint32_t index = 0; index < topSongs->Size(); ++index)
            {
                auto value = topSongs->GetAt(index);
                auto track = value.ValueType() == JsonValueType::Object
                    ? ParseTrack(value.GetObject(), result.Artist.Name, result.Artist.Provider)
                    : std::nullopt;
                if (!track)
                {
                    return std::nullopt;
                }
                result.TopSongs.push_back(std::move(*track));
            }
            for (std::uint32_t index = 0; index < releases->Size(); ++index)
            {
                auto value = releases->GetAt(index);
                auto release = value.ValueType() == JsonValueType::Object
                    ? ParseRelease(value.GetObject())
                    : std::nullopt;
                if (!release)
                {
                    return std::nullopt;
                }
                result.Releases.push_back(std::move(*release));
            }
            for (std::uint32_t index = 0; index < relatedArtists->Size(); ++index)
            {
                auto value = relatedArtists->GetAt(index);
                auto related = value.ValueType() == JsonValueType::Object
                    ? ParseArtistSummary(value.GetObject())
                    : std::nullopt;
                if (!related)
                {
                    return std::nullopt;
                }
                result.RelatedArtists.push_back(std::move(*related));
            }
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<RemoteAlbumDetail> ParseRemoteAlbumDetail(
        winrt::hstring const& payload) noexcept
    {
        try
        {
            auto root = ParseRoot(payload);
            auto releaseObject = root ? Object(*root, L"release") : std::nullopt;
            auto release = releaseObject ? ParseRelease(*releaseObject) : std::nullopt;
            auto tracks = root ? Array(*root, L"tracks", MaxAlbumTrackCount) : std::nullopt;
            auto stale = root ? Boolean(*root, L"stale") : std::nullopt;
            auto fetchedAt = root ? BoundedString(*root, L"fetchedAt", 64, true) : std::nullopt;
            if (!root || !releaseObject || !release || !tracks || !stale || !fetchedAt
                || !IsRfc3339(*fetchedAt))
            {
                return std::nullopt;
            }

            RemoteAlbumDetail result;
            result.Release = *release;
            result.Stale = *stale;
            result.FetchedAt = *fetchedAt;
            for (std::uint32_t index = 0; index < tracks->Size(); ++index)
            {
                auto value = tracks->GetAt(index);
                auto track = value.ValueType() == JsonValueType::Object
                    ? ParseTrack(value.GetObject(), release->Release.Subtitle, release->Release.Provider)
                    : std::nullopt;
                if (!track)
                {
                    return std::nullopt;
                }
                result.Tracks.push_back(std::move(*track));
            }
            return result;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
