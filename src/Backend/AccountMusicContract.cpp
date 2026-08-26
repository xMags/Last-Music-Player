#include "pch.h"
#include "Backend/AccountMusicContract.h"

#include "Backend/AccountUrlPolicy.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <unordered_set>

#include <winrt/Windows.Data.Json.h>

namespace LastMusicPlayer::Backend
{
    namespace Json = winrt::Windows::Data::Json;

    namespace
    {
        winrt::hstring StringValue(Json::JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return {};
            }
            auto const value = object.GetNamedValue(key);
            return value.ValueType() == Json::JsonValueType::String
                ? value.GetString()
                : winrt::hstring{};
        }

        winrt::hstring FirstString(
            Json::JsonObject const& object,
            std::initializer_list<wchar_t const*> keys)
        {
            for (auto const key : keys)
            {
                auto const value = StringValue(object, key);
                if (!value.empty())
                {
                    return value;
                }
            }
            return {};
        }

        double NumberValue(
            Json::JsonObject const& object,
            wchar_t const* key,
            double fallback = 0.0)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto const value = object.GetNamedValue(key);
            return value.ValueType() == Json::JsonValueType::Number
                ? value.GetNumber()
                : fallback;
        }

        bool BooleanValue(
            Json::JsonObject const& object,
            wchar_t const* key,
            bool fallback = false)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto const value = object.GetNamedValue(key);
            return value.ValueType() == Json::JsonValueType::Boolean
                ? value.GetBoolean()
                : fallback;
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

        Json::JsonArray ArrayValue(
            Json::JsonObject const& object,
            std::initializer_list<wchar_t const*> keys)
        {
            for (auto const key : keys)
            {
                if (!object || !object.HasKey(key))
                {
                    continue;
                }
                auto const value = object.GetNamedValue(key);
                if (value.ValueType() == Json::JsonValueType::Array)
                {
                    return value.GetArray();
                }
            }
            return nullptr;
        }

        Json::JsonObject TrackObjectFrom(Json::JsonObject const& wrapper)
        {
            auto const nested = ObjectValue(wrapper, L"track");
            return nested ? nested : wrapper;
        }

        void InsertString(
            Json::JsonObject const& object,
            wchar_t const* key,
            std::wstring const& value)
        {
            object.Insert(key, Json::JsonValue::CreateStringValue(winrt::hstring(value)));
        }

        Json::JsonObject PortableTrackObject(PortableAccountTrack const& track)
        {
            Json::JsonObject object;
            InsertString(object, L"id", track.Id);
            InsertString(object, L"title", track.Title);
            InsertString(object, L"artist", track.Artist);
            InsertString(object, L"album", track.Album);
            InsertString(object, L"genre", track.Genre);
            InsertString(
                object,
                L"artworkUrl",
                std::wstring(DurableArtworkUrl(winrt::hstring(track.ArtworkUrl)).c_str()));
            InsertString(object, L"sourceKind", track.SourceKind);
            InsertString(object, L"provider", track.Provider);
            InsertString(object, L"sourceUrl", track.SourceUrl);
            InsertString(object, L"catalogId", track.CatalogId);
            InsertString(object, L"albumCatalogId", track.AlbumCatalogId);
            InsertString(object, L"artistCatalogId", track.ArtistCatalogId);
            object.Insert(
                L"durationSec",
                Json::JsonValue::CreateNumberValue((std::max)(0.0, track.DurationSec)));
            return object;
        }

        std::optional<PortableAccountTrack> ParsePortableTrackObject(
            Json::JsonObject const& wrapper)
        {
            auto const object = TrackObjectFrom(wrapper);
            if (!object)
            {
                return std::nullopt;
            }

            PortableAccountTrack track;
            track.Id = FirstString(object, { L"id", L"remoteId", L"trackId" }).c_str();
            if (track.Id.empty())
            {
                track.Id = FirstString(wrapper, { L"trackId", L"remoteId", L"id" }).c_str();
            }
            track.Title = FirstString(object, { L"title", L"name" }).c_str();
            track.Artist = FirstString(object, { L"artist", L"artistName" }).c_str();
            track.Album = FirstString(object, { L"album", L"albumName" }).c_str();
            track.Genre = StringValue(object, L"genre").c_str();
            track.ArtworkUrl = FirstString(object, { L"artworkUrl", L"imageUrl" }).c_str();
            if (track.ArtworkUrl.empty() && object != wrapper)
            {
                track.ArtworkUrl = FirstString(wrapper, { L"artworkUrl", L"imageUrl" }).c_str();
            }
            track.ArtworkUrl = DurableArtworkUrl(winrt::hstring(track.ArtworkUrl)).c_str();
            track.SourceKind = StringValue(object, L"sourceKind").c_str();
            track.Provider = StringValue(object, L"provider").c_str();
            track.SourceUrl = FirstString(object, { L"sourceUrl", L"url" }).c_str();
            track.CatalogId = FirstString(object, { L"catalogId", L"sourceId" }).c_str();
            track.AlbumCatalogId = FirstString(object, { L"albumCatalogId", L"albumId" }).c_str();
            track.ArtistCatalogId = FirstString(object, { L"artistCatalogId", L"artistId" }).c_str();
            track.DurationSec = NumberValue(object, L"durationSec", 0.0);
            if (track.DurationSec <= 0.0)
            {
                // These two aliases only read cached data and responses emitted
                // by older desktop builds. New requests always use durationSec.
                track.DurationSec = NumberValue(object, L"durationSeconds", 0.0);
            }
            if (track.DurationSec <= 0.0)
            {
                track.DurationSec = NumberValue(object, L"durationMs", 0.0) / 1000.0;
            }
            track.DurationSec = (std::max)(0.0, track.DurationSec);
            if (track.SourceKind.empty())
            {
                track.SourceKind = L"remote";
            }
            return IsValidPortableTrack(track)
                ? std::optional<PortableAccountTrack>{ std::move(track) }
                : std::nullopt;
        }

        std::wstring Lower(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        std::wstring UpperHex(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towupper(character));
            });
            return value;
        }

        bool IsHexAccent(std::wstring const& value) noexcept
        {
            return value.size() == 7 && value.front() == L'#'
                && std::all_of(value.begin() + 1, value.end(), [](wchar_t character)
                {
                    return (character >= L'0' && character <= L'9')
                        || (character >= L'a' && character <= L'f')
                        || (character >= L'A' && character <= L'F');
                });
        }

        bool IsStorefront(std::wstring const& value) noexcept
        {
            return value.size() == 2
                && std::all_of(value.begin(), value.end(), [](wchar_t character)
                {
                    return character >= L'a' && character <= L'z';
                });
        }

        std::optional<std::wstring> CanonicalStringPreference(
            PortablePreferenceKey key,
            std::wstring value)
        {
            switch (key)
            {
            case PortablePreferenceKey::Theme:
                value = Lower(std::move(value));
                if (value == L"system" || value == L"light" || value == L"dark")
                {
                    return value;
                }
                break;
            case PortablePreferenceKey::Accent:
                if (IsHexAccent(value))
                {
                    return UpperHex(std::move(value));
                }
                break;
            case PortablePreferenceKey::Storefront:
                value = Lower(std::move(value));
                if (IsStorefront(value))
                {
                    return value;
                }
                break;
            default:
                break;
            }
            return std::nullopt;
        }

        void AppendLibraryTrack(
            ParsedAccountLibrary& parsed,
            std::unordered_map<std::wstring, std::size_t>& indexes,
            Json::JsonObject const& wrapper,
            bool liked,
            Json::JsonObject const& stats)
        {
            auto track = ParsePortableTrackObject(wrapper);
            if (!track)
            {
                return;
            }

            auto const key = track->Id;
            auto const found = indexes.find(key);
            auto const object = TrackObjectFrom(wrapper);
            auto addedAt = FirstString(wrapper, { L"addedAt" });
            if (addedAt.empty())
            {
                addedAt = FirstString(object, { L"addedAt", L"dateAdded" });
            }
            auto firstPlayed = FirstString(stats, { L"firstPlayedAt", L"firstPlayedAtUtc" });
            auto lastPlayed = FirstString(stats, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" });
            if (lastPlayed.empty())
            {
                lastPlayed = FirstString(wrapper, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" });
            }
            auto const playCount = static_cast<std::int64_t>((std::max)(
                0.0,
                NumberValue(stats, L"playCount", NumberValue(wrapper, L"playCount", 0.0))));
            auto lastPosition = NumberValue(
                stats,
                L"lastPositionSec",
                NumberValue(wrapper, L"lastPositionSec", -1.0));
            if (lastPosition < 0.0)
            {
                lastPosition = NumberValue(
                    stats,
                    L"lastPosition",
                    NumberValue(wrapper, L"position", 0.0));
            }

            if (found == indexes.end())
            {
                AccountLibraryTrackDto entry;
                entry.Track = std::move(*track);
                entry.Liked = liked
                    || BooleanValue(wrapper, L"liked", false)
                    || BooleanValue(object, L"liked", BooleanValue(object, L"isLiked", false));
                entry.PlayCount = playCount;
                entry.AddedAt = addedAt.c_str();
                entry.LegacyAddedSortKey = NumberValue(object, L"dateAddedSortKey", 0.0);
                entry.FirstPlayedAt = firstPlayed.c_str();
                entry.LastPlayedAt = lastPlayed.c_str();
                entry.LastPositionSec = (std::max)(0.0, lastPosition);
                indexes.emplace(key, parsed.Tracks.size());
                parsed.Tracks.push_back(std::move(entry));
                return;
            }

            auto& existing = parsed.Tracks[found->second];
            existing.Liked = existing.Liked || liked
                || BooleanValue(wrapper, L"liked", false)
                || BooleanValue(object, L"liked", BooleanValue(object, L"isLiked", false));
            existing.PlayCount = (std::max)(existing.PlayCount, playCount);
            if (existing.Track.ArtworkUrl.empty() && !track->ArtworkUrl.empty())
            {
                existing.Track.ArtworkUrl = std::move(track->ArtworkUrl);
            }
            if (existing.AddedAt.empty() && !addedAt.empty())
            {
                existing.AddedAt = addedAt.c_str();
            }
            if (!lastPlayed.empty() && std::wstring(lastPlayed.c_str()) > existing.LastPlayedAt)
            {
                existing.LastPlayedAt = lastPlayed.c_str();
                existing.LastPositionSec = (std::max)(0.0, lastPosition);
            }
        }
    }

    bool IsValidPortableTrack(PortableAccountTrack const& track) noexcept
    {
        return !track.Id.empty()
            && !track.Title.empty()
            && !track.SourceUrl.empty()
            && IsSyncableRemoteSource(winrt::hstring(track.SourceKind), winrt::hstring(track.SourceUrl));
    }

    winrt::hstring SerializePortableTrack(PortableAccountTrack const& track)
    {
        return IsValidPortableTrack(track)
            ? PortableTrackObject(track).Stringify()
            : winrt::hstring{};
    }

    std::optional<PortableAccountTrack> ParsePortableTrack(winrt::hstring const& payload) noexcept
    {
        try
        {
            Json::JsonObject root;
            if (!Json::JsonObject::TryParse(payload, root) || !root)
            {
                return std::nullopt;
            }
            return ParsePortableTrackObject(root);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    winrt::hstring SerializeHistoryBatch(std::vector<AccountHistoryEventDto> const& events)
    {
        Json::JsonArray values;
        for (auto const& event : events)
        {
            if (event.EventId.empty() || event.PlayedAt.empty() || !IsValidPortableTrack(event.Track))
            {
                continue;
            }
            Json::JsonObject value;
            InsertString(value, L"eventId", event.EventId);
            value.Insert(L"track", PortableTrackObject(event.Track));
            InsertString(value, L"playedAt", event.PlayedAt);
            value.Insert(
                L"positionSec",
                Json::JsonValue::CreateNumberValue(std::floor((std::max)(0.0, event.PositionSec))));
            values.Append(value);
        }
        Json::JsonObject root;
        root.Insert(L"events", values);
        return root.Stringify();
    }

    winrt::hstring WrapTrackRequest(winrt::hstring const& portableTrackJson)
    {
        Json::JsonObject track;
        if (!Json::JsonObject::TryParse(portableTrackJson, track) || !track
            || !ParsePortableTrackObject(track))
        {
            throw winrt::hresult_invalid_argument(L"Account track payload is invalid.");
        }
        Json::JsonObject root;
        root.Insert(L"track", track);
        return root.Stringify();
    }

    winrt::hstring SerializePlaylistReplacement(
        winrt::hstring const& name,
        std::vector<PortableAccountTrack> const& tracks)
    {
        Json::JsonObject root;
        if (!name.empty())
        {
            root.Insert(L"name", Json::JsonValue::CreateStringValue(name));
        }
        Json::JsonArray values;
        for (auto const& track : tracks)
        {
            if (IsValidPortableTrack(track))
            {
                values.Append(PortableTrackObject(track));
            }
        }
        root.Insert(L"tracks", values);
        return root.Stringify();
    }

    ParsedAccountLibrary ParseAccountLibrary(winrt::hstring const& payload) noexcept
    {
        try
        {
            Json::JsonObject root;
            if (!Json::JsonObject::TryParse(payload, root) || !root)
            {
                return {};
            }
            if (auto const library = ObjectValue(root, L"library"))
            {
                root = library;
            }
            if (auto const data = ObjectValue(root, L"data"))
            {
                root = data;
            }
            if (!root.HasKey(L"tracks") && !root.HasKey(L"songs")
                && !root.HasKey(L"liked") && !root.HasKey(L"likedTracks")
                && !root.HasKey(L"history") && !root.HasKey(L"recentlyPlayed")
                && !root.HasKey(L"playlists"))
            {
                return {};
            }

            ParsedAccountLibrary parsed;
            parsed.Valid = true;
            std::unordered_map<std::wstring, std::size_t> indexes;

            if (auto const tracks = ArrayValue(root, { L"tracks", L"songs" }))
            {
                for (std::uint32_t index = 0; index < tracks.Size(); ++index)
                {
                    auto const value = tracks.GetAt(index);
                    if (value.ValueType() == Json::JsonValueType::Object)
                    {
                        auto const object = value.GetObject();
                        AppendLibraryTrack(parsed, indexes, object, false, object);
                    }
                }
            }
            if (auto const liked = ArrayValue(
                root,
                { L"liked", L"likedTracks", L"favourites", L"favorites" }))
            {
                for (std::uint32_t index = 0; index < liked.Size(); ++index)
                {
                    auto const value = liked.GetAt(index);
                    if (value.ValueType() == Json::JsonValueType::Object)
                    {
                        auto const object = value.GetObject();
                        AppendLibraryTrack(parsed, indexes, object, true, object);
                    }
                }
            }
            if (auto const history = ArrayValue(root, { L"history", L"recentlyPlayed", L"plays" }))
            {
                for (std::uint32_t index = 0; index < history.Size(); ++index)
                {
                    auto const value = history.GetAt(index);
                    if (value.ValueType() == Json::JsonValueType::Object)
                    {
                        auto const object = value.GetObject();
                        auto const stats = ObjectValue(object, L"stats");
                        AppendLibraryTrack(parsed, indexes, object, false, stats ? stats : object);
                    }
                }
            }

            if (auto const playlists = ArrayValue(root, { L"playlists" }))
            {
                std::unordered_set<std::wstring> playlistIds;
                for (std::uint32_t index = 0; index < playlists.Size(); ++index)
                {
                    auto const value = playlists.GetAt(index);
                    if (value.ValueType() != Json::JsonValueType::Object)
                    {
                        continue;
                    }
                    auto const object = value.GetObject();
                    AccountPlaylistDto playlist;
                    playlist.Id = FirstString(object, { L"id", L"playlistId" }).c_str();
                    if (playlist.Id.empty() || !playlistIds.insert(playlist.Id).second)
                    {
                        continue;
                    }
                    playlist.Name = FirstString(object, { L"name", L"title" }).c_str();
                    playlist.Description = StringValue(object, L"description").c_str();
                    playlist.SourceUrl = StringValue(object, L"sourceUrl").c_str();
                    playlist.UpdatedAt = FirstString(object, { L"updatedAt", L"createdAt" }).c_str();
                    std::unordered_set<std::wstring> trackIds;
                    if (auto const tracks = ArrayValue(object, { L"tracks", L"items", L"songs" }))
                    {
                        for (std::uint32_t trackIndex = 0; trackIndex < tracks.Size(); ++trackIndex)
                        {
                            auto const trackValue = tracks.GetAt(trackIndex);
                            std::wstring trackId;
                            if (trackValue.ValueType() == Json::JsonValueType::String)
                            {
                                trackId = trackValue.GetString().c_str();
                            }
                            else if (trackValue.ValueType() == Json::JsonValueType::Object)
                            {
                                auto const trackObject = trackValue.GetObject();
                                auto const parsedTrack = ParsePortableTrackObject(trackObject);
                                if (parsedTrack)
                                {
                                    trackId = parsedTrack->Id;
                                    AppendLibraryTrack(parsed, indexes, trackObject, false, trackObject);
                                }
                            }
                            if (!trackId.empty() && indexes.contains(trackId) && trackIds.insert(trackId).second)
                            {
                                playlist.TrackIds.push_back(std::move(trackId));
                            }
                        }
                    }
                    parsed.Playlists.push_back(std::move(playlist));
                }
            }
            return parsed;
        }
        catch (...)
        {
            return {};
        }
    }

    std::vector<std::wstring> ParseAcknowledgedHistoryEvents(
        winrt::hstring const& payload,
        std::vector<std::wstring> const& submittedEventIds) noexcept
    {
        try
        {
            Json::JsonObject root;
            if (!Json::JsonObject::TryParse(payload, root) || !root)
            {
                return {};
            }
            std::unordered_set<std::wstring> submitted{
                submittedEventIds.begin(),
                submittedEventIds.end()
            };
            std::unordered_set<std::wstring> seen;
            std::vector<std::wstring> result;
            for (auto const key : { L"accepted", L"duplicates" })
            {
                auto const values = ArrayValue(root, { key });
                if (!values)
                {
                    continue;
                }
                for (std::uint32_t index = 0; index < values.Size(); ++index)
                {
                    auto const value = values.GetAt(index);
                    if (value.ValueType() != Json::JsonValueType::String)
                    {
                        continue;
                    }
                    auto id = std::wstring(value.GetString().c_str());
                    if (submitted.contains(id) && seen.insert(id).second)
                    {
                        result.push_back(std::move(id));
                    }
                }
            }
            return result;
        }
        catch (...)
        {
            return {};
        }
    }

    wchar_t const* PortablePreferenceName(PortablePreferenceKey key) noexcept
    {
        switch (key)
        {
        case PortablePreferenceKey::Theme: return L"theme";
        case PortablePreferenceKey::Accent: return L"accent";
        case PortablePreferenceKey::GaplessPlayback: return L"gaplessPlayback";
        case PortablePreferenceKey::DataSaver: return L"dataSaver";
        case PortablePreferenceKey::Storefront: return L"storefront";
        }
        return L"";
    }

    std::optional<PortablePreferenceKey> ParsePortablePreferenceName(std::wstring const& name) noexcept
    {
        if (name == L"theme") return PortablePreferenceKey::Theme;
        if (name == L"accent") return PortablePreferenceKey::Accent;
        if (name == L"gaplessPlayback") return PortablePreferenceKey::GaplessPlayback;
        if (name == L"dataSaver") return PortablePreferenceKey::DataSaver;
        if (name == L"storefront") return PortablePreferenceKey::Storefront;
        return std::nullopt;
    }

    PortablePreferences ParsePortablePreferences(winrt::hstring const& payload) noexcept
    {
        PortablePreferences result;
        try
        {
            Json::JsonObject root;
            if (!Json::JsonObject::TryParse(payload, root) || !root)
            {
                return result;
            }
            for (int depth = 0; depth < 2; ++depth)
            {
                if (auto const data = ObjectValue(root, L"data"))
                {
                    root = data;
                    continue;
                }
                if (auto const preferences = ObjectValue(root, L"preferences"))
                {
                    root = preferences;
                    continue;
                }
                break;
            }

            auto readString = [&](PortablePreferenceKey key) -> std::optional<std::wstring>
            {
                auto const name = PortablePreferenceName(key);
                if (!root.HasKey(name))
                {
                    return std::nullopt;
                }
                auto const value = root.GetNamedValue(name);
                if (value.ValueType() != Json::JsonValueType::String)
                {
                    return std::nullopt;
                }
                return CanonicalStringPreference(key, std::wstring(value.GetString().c_str()));
            };
            auto readBoolean = [&](PortablePreferenceKey key) -> std::optional<bool>
            {
                auto const name = PortablePreferenceName(key);
                if (!root.HasKey(name))
                {
                    return std::nullopt;
                }
                auto const value = root.GetNamedValue(name);
                return value.ValueType() == Json::JsonValueType::Boolean
                    ? std::optional<bool>{ value.GetBoolean() }
                    : std::nullopt;
            };

            result.Theme = readString(PortablePreferenceKey::Theme);
            result.Accent = readString(PortablePreferenceKey::Accent);
            result.GaplessPlayback = readBoolean(PortablePreferenceKey::GaplessPlayback);
            result.DataSaver = readBoolean(PortablePreferenceKey::DataSaver);
            result.Storefront = readString(PortablePreferenceKey::Storefront);
        }
        catch (...)
        {
            return {};
        }
        return result;
    }

    winrt::hstring SerializePortablePreferences(PortablePreferences const& preferences)
    {
        Json::JsonObject root;
        auto insertString = [&](PortablePreferenceKey key, std::optional<std::wstring> const& value)
        {
            if (value)
            {
                root.Insert(
                    PortablePreferenceName(key),
                    Json::JsonValue::CreateStringValue(winrt::hstring(*value)));
            }
        };
        auto insertBoolean = [&](PortablePreferenceKey key, std::optional<bool> value)
        {
            if (value)
            {
                root.Insert(
                    PortablePreferenceName(key),
                    Json::JsonValue::CreateBooleanValue(*value));
            }
        };
        insertString(PortablePreferenceKey::Theme, preferences.Theme);
        insertString(PortablePreferenceKey::Accent, preferences.Accent);
        insertBoolean(PortablePreferenceKey::GaplessPlayback, preferences.GaplessPlayback);
        insertBoolean(PortablePreferenceKey::DataSaver, preferences.DataSaver);
        insertString(PortablePreferenceKey::Storefront, preferences.Storefront);
        return root.Stringify();
    }

    std::unordered_map<PortablePreferenceKey, std::wstring> PortablePreferenceValues(
        PortablePreferences const& preferences)
    {
        std::unordered_map<PortablePreferenceKey, std::wstring> values;
        if (preferences.Theme) values.emplace(PortablePreferenceKey::Theme, L"\"" + *preferences.Theme + L"\"");
        if (preferences.Accent) values.emplace(PortablePreferenceKey::Accent, L"\"" + *preferences.Accent + L"\"");
        if (preferences.GaplessPlayback) values.emplace(
            PortablePreferenceKey::GaplessPlayback,
            *preferences.GaplessPlayback ? L"true" : L"false");
        if (preferences.DataSaver) values.emplace(
            PortablePreferenceKey::DataSaver,
            *preferences.DataSaver ? L"true" : L"false");
        if (preferences.Storefront) values.emplace(PortablePreferenceKey::Storefront, L"\"" + *preferences.Storefront + L"\"");
        return values;
    }

    PortablePreferences PortablePreferencesFromValues(
        std::unordered_map<PortablePreferenceKey, std::wstring> const& values) noexcept
    {
        PortablePreferences preferences;
        for (auto const& [key, value] : values)
        {
            auto const validated = ValidatePortablePreferenceValue(key, value);
            if (!validated)
            {
                continue;
            }
            Json::JsonValue json{ nullptr };
            if (!Json::JsonValue::TryParse(winrt::hstring(*validated), json))
            {
                continue;
            }
            switch (key)
            {
            case PortablePreferenceKey::Theme:
                preferences.Theme = std::wstring(json.GetString().c_str());
                break;
            case PortablePreferenceKey::Accent:
                preferences.Accent = std::wstring(json.GetString().c_str());
                break;
            case PortablePreferenceKey::GaplessPlayback:
                preferences.GaplessPlayback = json.GetBoolean();
                break;
            case PortablePreferenceKey::DataSaver:
                preferences.DataSaver = json.GetBoolean();
                break;
            case PortablePreferenceKey::Storefront:
                preferences.Storefront = std::wstring(json.GetString().c_str());
                break;
            }
        }
        return preferences;
    }

    std::optional<std::wstring> ValidatePortablePreferenceValue(
        PortablePreferenceKey key,
        std::wstring const& jsonValue) noexcept
    {
        try
        {
            Json::JsonValue value{ nullptr };
            if (!Json::JsonValue::TryParse(winrt::hstring(jsonValue), value))
            {
                return std::nullopt;
            }
            if (key == PortablePreferenceKey::GaplessPlayback
                || key == PortablePreferenceKey::DataSaver)
            {
                if (value.ValueType() != Json::JsonValueType::Boolean)
                {
                    return std::nullopt;
                }
                return value.GetBoolean() ? std::wstring{ L"true" } : std::wstring{ L"false" };
            }
            if (value.ValueType() != Json::JsonValueType::String)
            {
                return std::nullopt;
            }
            auto const canonical = CanonicalStringPreference(
                key,
                std::wstring(value.GetString().c_str()));
            if (!canonical)
            {
                return std::nullopt;
            }
            return std::wstring{ L"\"" } + *canonical + L"\"";
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
}
