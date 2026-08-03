#include "pch.h"
#include "Backend/MusicSyncService.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <winrt/Windows.Data.Json.h>

namespace LastMusicPlayer::Backend
{
    using namespace winrt::Windows::Data::Json;

    namespace
    {
        winrt::hstring JsonString(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return {};
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::String ? value.GetString() : winrt::hstring{};
        }

        double JsonNumber(JsonObject const& object, wchar_t const* key, double fallback = 0.0)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Number ? value.GetNumber() : fallback;
        }

        bool JsonBoolean(JsonObject const& object, wchar_t const* key, bool fallback = false)
        {
            if (!object || !object.HasKey(key))
            {
                return fallback;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Boolean ? value.GetBoolean() : fallback;
        }

        JsonObject JsonObjectValue(JsonObject const& object, wchar_t const* key)
        {
            if (!object || !object.HasKey(key))
            {
                return nullptr;
            }
            auto value = object.GetNamedValue(key);
            return value.ValueType() == JsonValueType::Object ? value.GetObject() : nullptr;
        }

        JsonArray JsonArrayValue(JsonObject const& object, std::initializer_list<wchar_t const*> keys)
        {
            for (auto key : keys)
            {
                if (!object || !object.HasKey(key))
                {
                    continue;
                }
                auto value = object.GetNamedValue(key);
                if (value.ValueType() == JsonValueType::Array)
                {
                    return value.GetArray();
                }
            }
            return nullptr;
        }

        // Keep every durable remote source that can be resolved by the account
        // service. Filtering by a specific upstream catalog would make libraries
        // created on another client arrive partly empty without explanation.
        bool IsCompatibleSource(JsonObject const& trackObject, winrt::hstring const& sourceUrl)
        {
            return IsSyncableRemoteSource(JsonString(trackObject, L"sourceKind"), sourceUrl);
        }

        winrt::hstring FirstString(JsonObject const& object, std::initializer_list<wchar_t const*> keys)
        {
            for (auto key : keys)
            {
                auto value = JsonString(object, key);
                if (!value.empty())
                {
                    return value;
                }
            }
            return {};
        }

        JsonObject TrackObjectFrom(JsonObject const& wrapper)
        {
            auto nested = JsonObjectValue(wrapper, L"track");
            return nested ? nested : wrapper;
        }

        winrt::hstring RemoteIdFrom(JsonObject const& wrapper, JsonObject const& track)
        {
            auto id = FirstString(track, { L"id", L"remoteId", L"trackId" });
            return id.empty() ? FirstString(wrapper, { L"trackId", L"remoteId", L"id" }) : id;
        }

        winrt::hstring FormatDuration(double seconds)
        {
            if (seconds <= 0.0)
            {
                return {};
            }
            auto whole = static_cast<long long>(seconds);
            auto minutes = whole / 60;
            auto remainder = whole % 60;
            wchar_t value[32]{};
            swprintf_s(value, L"%lld:%02lld", minutes, remainder);
            return value;
        }

        std::wstring UtcNow()
        {
            SYSTEMTIME now{};
            ::GetSystemTime(&now);
            wchar_t value[40]{};
            swprintf_s(
                value,
                L"%04u-%02u-%02uT%02u:%02u:%02uZ",
                now.wYear,
                now.wMonth,
                now.wDay,
                now.wHour,
                now.wMinute,
                now.wSecond);
            return value;
        }

        winrt::hstring TrackDto(TrackInfo const& track)
        {
            JsonObject object;
            object.Insert(L"id", JsonValue::CreateStringValue(track.RemoteId()));
            object.Insert(L"title", JsonValue::CreateStringValue(track.Title()));
            object.Insert(L"artist", JsonValue::CreateStringValue(track.Artist()));
            object.Insert(L"album", JsonValue::CreateStringValue(track.Album()));
            object.Insert(L"artworkUrl", JsonValue::CreateStringValue(
                IsSafeRemoteUrl(track.ArtworkUrl(), RemoteUrlUse::Durable) ? track.ArtworkUrl() : winrt::hstring{}));
            object.Insert(L"sourceUrl", JsonValue::CreateStringValue(track.SourceUrl()));
            object.Insert(L"durationMs", JsonValue::CreateNumberValue((std::max)(0.0, track.DurationSeconds()) * 1000.0));
            return object.Stringify();
        }

        struct ParsedLibrary
        {
            AccountLibrarySnapshot Snapshot;
            bool Valid{};
        };

        ParsedLibrary ParseLibrary(
            winrt::hstring const& payload,
            winrt::hstring const& ownerId,
            AccountProfile const& profile,
            std::wstring const& synchronizedAt)
        {
            if (ownerId.empty() || profile.Id != ownerId)
            {
                return {};
            }

            JsonObject root;
            if (!JsonObject::TryParse(payload, root) || !root)
            {
                return {};
            }
            auto nestedLibrary = JsonObjectValue(root, L"library");
            if (nestedLibrary)
            {
                root = nestedLibrary;
            }
            auto nestedData = JsonObjectValue(root, L"data");
            if (nestedData)
            {
                root = nestedData;
            }

            auto recognizedShape = root.HasKey(L"tracks") || root.HasKey(L"songs")
                || root.HasKey(L"liked") || root.HasKey(L"likedTracks")
                || root.HasKey(L"favourites") || root.HasKey(L"favorites")
                || root.HasKey(L"history") || root.HasKey(L"recentlyPlayed") || root.HasKey(L"plays")
                || root.HasKey(L"playlists");
            if (!recognizedShape)
            {
                return {};
            }

            ParsedLibrary parsed;
            parsed.Valid = true;
            parsed.Snapshot.Profile.AccountId = std::wstring(ownerId.c_str());
            parsed.Snapshot.Profile.DisplayName = std::wstring(profile.DisplayName.c_str());
            parsed.Snapshot.Profile.Username = std::wstring(profile.Username.c_str());
            parsed.Snapshot.Profile.AvatarUrl = std::wstring(profile.AvatarUrl.c_str());
            parsed.Snapshot.Profile.PlanLabel = std::wstring(profile.PlanLabel.c_str());
            parsed.Snapshot.Profile.UpdatedAtUtc = synchronizedAt;
            parsed.Snapshot.SynchronizedAtUtc = synchronizedAt;

            std::unordered_map<std::wstring, size_t> trackIndexes;
            size_t sequence{};
            auto addTrack = [&](JsonObject const& wrapper, bool liked, JsonObject const& stats) -> winrt::hstring
            {
                auto object = TrackObjectFrom(wrapper);
                if (!object)
                {
                    return {};
                }
                auto remoteId = RemoteIdFrom(wrapper, object);
                auto sourceUrl = FirstString(object, { L"sourceUrl", L"url" });
                auto title = FirstString(object, { L"title", L"name" });
                auto artworkUrl = FirstString(object, { L"artworkUrl", L"imageUrl" });
                if (artworkUrl.empty() && object != wrapper)
                {
                    artworkUrl = FirstString(wrapper, { L"artworkUrl", L"imageUrl" });
                }
                if (remoteId.empty() || title.empty() || sourceUrl.empty() || !IsCompatibleSource(object, sourceUrl))
                {
                    return {};
                }

                auto key = std::wstring(remoteId.c_str());
                auto found = trackIndexes.find(key);
                if (found == trackIndexes.end())
                {
                    TrackInfo track;
                    track.RemoteId(remoteId);
                    track.SourceKind(L"remote");
                    track.Provider(L"account");
                    track.SourceLabel(L"Account");
                    track.SourceUrl(sourceUrl);
                    track.FilePath(L"");
                    track.Title(title);
                    track.Artist(FirstString(object, { L"artist", L"artistName" }));
                    track.Album(FirstString(object, { L"album", L"albumName" }));
                    track.Genre(FirstString(object, { L"genre" }));
                    auto durationSeconds = JsonNumber(object, L"durationSeconds", 0.0);
                    if (durationSeconds <= 0.0)
                    {
                        durationSeconds = JsonNumber(object, L"durationMs", 0.0) / 1000.0;
                    }
                    track.DurationSeconds((std::max)(0.0, durationSeconds));
                    track.Duration(FormatDuration(track.DurationSeconds()));
                    track.ArtworkUrl(artworkUrl);
                    track.DateAdded(L"Synced");
                    auto dateSort = JsonNumber(object, L"dateAddedSortKey", 0.0);
                    track.DateAddedSortKey(dateSort > 0.0 ? dateSort : -static_cast<double>(sequence++));
                    track.IsLiked(liked || JsonBoolean(object, L"isLiked", false));

                    AccountTrackSnapshot entry;
                    entry.Track = track;
                    entry.PlayCount = static_cast<int64_t>((std::max)(0.0, JsonNumber(stats, L"playCount", JsonNumber(wrapper, L"playCount", 0.0))));
                    entry.FirstPlayedAtUtc = std::wstring(FirstString(stats, { L"firstPlayedAt", L"firstPlayedAtUtc" }).c_str());
                    entry.LastPlayedAtUtc = std::wstring(FirstString(stats, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" }).c_str());
                    if (entry.LastPlayedAtUtc.empty())
                    {
                        entry.LastPlayedAtUtc = std::wstring(FirstString(wrapper, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" }).c_str());
                    }
                    entry.LastPositionSeconds = (std::max)(0.0, JsonNumber(stats, L"lastPosition", JsonNumber(wrapper, L"position", 0.0)));
                    trackIndexes.emplace(key, parsed.Snapshot.Tracks.size());
                    parsed.Snapshot.Tracks.push_back(std::move(entry));
                    return remoteId;
                }

                auto& existing = parsed.Snapshot.Tracks[found->second];
                existing.Track.IsLiked(existing.Track.IsLiked() || liked || JsonBoolean(object, L"isLiked", false));
                if (existing.Track.ArtworkUrl().empty() && !artworkUrl.empty())
                {
                    existing.Track.ArtworkUrl(artworkUrl);
                }
                existing.PlayCount = (std::max)(existing.PlayCount,
                    static_cast<int64_t>((std::max)(0.0, JsonNumber(stats, L"playCount", JsonNumber(wrapper, L"playCount", 0.0)))));
                auto lastPlayed = FirstString(stats, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" });
                if (lastPlayed.empty())
                {
                    lastPlayed = FirstString(wrapper, { L"lastPlayedAt", L"playedAt", L"playedAtUtc" });
                }
                if (!lastPlayed.empty() && std::wstring(lastPlayed.c_str()) > existing.LastPlayedAtUtc)
                {
                    existing.LastPlayedAtUtc = std::wstring(lastPlayed.c_str());
                    existing.LastPositionSeconds = (std::max)(0.0, JsonNumber(stats, L"lastPosition", JsonNumber(wrapper, L"position", 0.0)));
                }
                return remoteId;
            };

            auto allTracks = JsonArrayValue(root, { L"tracks", L"songs" });
            if (allTracks)
            {
                for (uint32_t index = 0; index < allTracks.Size(); ++index)
                {
                    auto value = allTracks.GetAt(index);
                    if (value.ValueType() == JsonValueType::Object)
                    {
                        auto object = value.GetObject();
                        addTrack(object, JsonBoolean(object, L"isLiked", false), object);
                    }
                }
            }

            auto liked = JsonArrayValue(root, { L"liked", L"likedTracks", L"favourites", L"favorites" });
            if (liked)
            {
                for (uint32_t index = 0; index < liked.Size(); ++index)
                {
                    auto value = liked.GetAt(index);
                    if (value.ValueType() == JsonValueType::Object)
                    {
                        auto object = value.GetObject();
                        addTrack(object, true, object);
                    }
                }
            }

            auto history = JsonArrayValue(root, { L"history", L"recentlyPlayed", L"plays" });
            if (history)
            {
                for (uint32_t index = 0; index < history.Size(); ++index)
                {
                    auto value = history.GetAt(index);
                    if (value.ValueType() == JsonValueType::Object)
                    {
                        auto object = value.GetObject();
                        auto stats = JsonObjectValue(object, L"stats");
                        addTrack(object, JsonBoolean(object, L"isLiked", false), stats ? stats : object);
                    }
                }
            }

            auto playlists = JsonArrayValue(root, { L"playlists" });
            if (playlists)
            {
                std::unordered_set<std::wstring> playlistIds;
                for (uint32_t index = 0; index < playlists.Size(); ++index)
                {
                    auto value = playlists.GetAt(index);
                    if (value.ValueType() != JsonValueType::Object)
                    {
                        continue;
                    }
                    auto object = value.GetObject();
                    auto playlistId = FirstString(object, { L"id", L"playlistId" });
                    if (playlistId.empty() || !playlistIds.insert(std::wstring(playlistId.c_str())).second)
                    {
                        continue;
                    }

                    AccountPlaylistSnapshot playlist;
                    playlist.PlaylistId = std::wstring(playlistId.c_str());
                    playlist.Name = std::wstring(FirstString(object, { L"name", L"title" }).c_str());
                    playlist.Description = std::wstring(JsonString(object, L"description").c_str());
                    auto sourceUrl = JsonString(object, L"sourceUrl");
                    if (IsSafeRemoteUrl(sourceUrl, RemoteUrlUse::Durable))
                    {
                        playlist.SourceUrl = std::wstring(sourceUrl.c_str());
                    }
                    playlist.UpdatedAtUtc = std::wstring(FirstString(object, { L"updatedAt", L"createdAt" }).c_str());
                    if (playlist.UpdatedAtUtc.empty())
                    {
                        playlist.UpdatedAtUtc = synchronizedAt;
                    }

                    auto tracks = JsonArrayValue(object, { L"tracks", L"items", L"songs" });
                    if (tracks)
                    {
                        std::unordered_set<std::wstring> playlistTrackIds;
                        for (uint32_t trackIndex = 0; trackIndex < tracks.Size(); ++trackIndex)
                        {
                            auto trackValue = tracks.GetAt(trackIndex);
                            winrt::hstring remoteId;
                            if (trackValue.ValueType() == JsonValueType::String)
                            {
                                remoteId = trackValue.GetString();
                            }
                            else if (trackValue.ValueType() == JsonValueType::Object)
                            {
                                auto trackObject = trackValue.GetObject();
                                remoteId = addTrack(trackObject, JsonBoolean(trackObject, L"isLiked", false), trackObject);
                            }
                            auto key = std::wstring(remoteId.c_str());
                            if (!key.empty() && trackIndexes.contains(key) && playlistTrackIds.insert(key).second)
                            {
                                playlist.RemoteTrackIds.push_back(std::move(key));
                            }
                        }
                    }
                    parsed.Snapshot.Playlists.push_back(std::move(playlist));
                }
            }

            return parsed;
        }

        std::vector<std::wstring> ParseAcknowledgedEvents(
            winrt::hstring const& payload,
            std::unordered_set<std::wstring> const& submittedEventIds)
        {
            JsonObject root;
            if (!JsonObject::TryParse(payload, root) || !root)
            {
                return {};
            }
            std::vector<std::wstring> result;
            std::unordered_set<std::wstring> seen;
            for (auto key : { L"accepted", L"duplicates" })
            {
                auto values = JsonArrayValue(root, { key });
                if (!values)
                {
                    continue;
                }
                for (uint32_t index = 0; index < values.Size(); ++index)
                {
                    auto value = values.GetAt(index);
                    if (value.ValueType() != JsonValueType::String)
                    {
                        continue;
                    }
                    auto id = std::wstring(value.GetString().c_str());
                    if (submittedEventIds.contains(id) && seen.insert(id).second)
                    {
                        result.push_back(std::move(id));
                    }
                }
            }
            return result;
        }

        winrt::hstring BuildHistoryBatch(std::vector<PlaybackEventRecord> const& events)
        {
            JsonArray values;
            for (auto const& event : events)
            {
                if (!event.Track || event.EventId.empty() || event.PlayedAtUtc.empty())
                {
                    continue;
                }
                JsonObject value;
                value.Insert(L"eventId", JsonValue::CreateStringValue(winrt::hstring(event.EventId)));
                JsonObject track;
                JsonObject::TryParse(TrackDto(event.Track), track);
                value.Insert(L"track", track);
                value.Insert(L"playedAt", JsonValue::CreateStringValue(winrt::hstring(event.PlayedAtUtc)));
                value.Insert(L"position", JsonValue::CreateNumberValue(std::floor((std::max)(0.0, event.PositionSeconds))));
                values.Append(value);
            }
            JsonObject root;
            root.Insert(L"events", values);
            return root.Stringify();
        }
    }

    MusicSyncService::MusicSyncService(
        RemoteMusicService& remoteMusic,
        DatabaseEngine& database,
        UserDataOperationGate& operationGate)
        : m_remoteMusic(remoteMusic),
          m_database(database),
          m_operationGate(operationGate)
    {
    }

    bool MusicSyncService::BeginSync() noexcept
    {
        std::lock_guard guard{ m_mutex };
        if (m_syncing)
        {
            return false;
        }
        m_syncing = true;
        m_lastSafeError = {};
        return true;
    }

    void MusicSyncService::FinishSync() noexcept
    {
        std::lock_guard guard{ m_mutex };
        m_syncing = false;
    }

    void MusicSyncService::SetSafeError(winrt::hstring const& message)
    {
        std::lock_guard guard{ m_mutex };
        m_lastSafeError = message;
    }

    bool MusicSyncService::IsSyncing() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_syncing;
    }

    winrt::hstring MusicSyncService::LastSafeError() const
    {
        std::lock_guard guard{ m_mutex };
        return m_lastSafeError;
    }

    bool MusicSyncService::SaveSyncError(
        AccountSyncContext const& context,
        std::wstring const& code)
    {
        if (!m_remoteMusic.IsCurrent(context))
        {
            return false;
        }

        auto accountId = std::wstring(context.OwnerId().c_str());
        auto state = m_database.LoadAccountSyncState(accountId);
        if (!m_remoteMusic.IsCurrent(context))
        {
            return false;
        }

        state.AccountId = accountId;
        state.LastSafeErrorCode = code;
        return m_remoteMusic.IsCurrent(context)
            && m_database.SaveAccountSyncState(state);
    }

    bool MusicSyncService::FailSync(
        AccountSyncContext const& context,
        winrt::hstring const& message,
        std::wstring const& code)
    {
        if (!m_remoteMusic.IsCurrent(context))
        {
            return false;
        }

        auto saved = SaveSyncError(context, code);
        if (m_remoteMusic.IsCurrent(context))
        {
            SetSafeError(message);
        }
        return saved;
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> MusicSyncService::SyncAsync()
    {
        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease || !BeginSync())
        {
            co_return false;
        }
        struct FinishGuard
        {
            MusicSyncService* Service;
            ~FinishGuard() { Service->FinishSync(); }
        } finish{ this };

        std::optional<AccountSyncContext> operation;
        try
        {
            operation.emplace(m_remoteMusic.CaptureAccountSyncContext());
            auto const& context = *operation;
            auto accountId = std::wstring(context.OwnerId().c_str());
            auto status = m_remoteMusic.StatusFor(context);
            auto profile = m_remoteMusic.ProfileFor(context);

            if (status == AccountSessionStatus::Offline)
            {
                FailSync(
                    context,
                    L"Offline. The cached account library is still available.",
                    L"offline");
                co_return false;
            }
            if (status != AccountSessionStatus::Validated)
            {
                FailSync(
                    context,
                    L"Sign in to synchronize the account library.",
                    L"signed-out");
                co_return false;
            }

            for (;;)
            {
                if (!m_remoteMusic.IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }

                auto events = m_database.LoadPendingPlaybackEvents(accountId, 100);
                if (events.empty())
                {
                    break;
                }

                std::vector<std::wstring> submitted;
                std::unordered_set<std::wstring> submittedSet;
                submitted.reserve(events.size());
                submittedSet.reserve(events.size());
                for (auto const& event : events)
                {
                    if (event.Track && !event.EventId.empty() && !event.PlayedAtUtc.empty()
                        && submittedSet.insert(event.EventId).second)
                    {
                        submitted.push_back(event.EventId);
                    }
                }
                if (submitted.empty())
                {
                    FailSync(
                        context,
                        L"A pending account history item could not be prepared.",
                        L"history-payload");
                    co_return false;
                }
                if (!m_remoteMusic.IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }
                if (!m_database.MarkPlaybackEventsAttempted(accountId, submitted))
                {
                    FailSync(
                        context,
                        L"Could not update the pending account history.",
                        L"history-attempt");
                    co_return false;
                }

                auto response = co_await m_remoteMusic.PostAccountHistoryBatchAsync(
                    context,
                    BuildHistoryBatch(events));
                if (!m_remoteMusic.IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }

                auto acknowledged = ParseAcknowledgedEvents(response, submittedSet);
                if (acknowledged.empty())
                {
                    FailSync(
                        context,
                        L"The account history response was not accepted.",
                        L"history-response");
                    co_return false;
                }
                if (!m_database.AcknowledgePlaybackEvents(accountId, acknowledged))
                {
                    FailSync(
                        context,
                        L"Could not acknowledge the synchronized account history.",
                        L"history-acknowledgement");
                    co_return false;
                }
                if (acknowledged.size() < submitted.size())
                {
                    break;
                }
            }

            if (!m_remoteMusic.IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            auto likes = m_database.LoadPendingLikes(accountId, 100);
            for (auto const& like : likes)
            {
                if (!m_remoteMusic.IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }
                if (!m_database.MarkPendingLikeAttempted(accountId, like.RemoteTrackId))
                {
                    FailSync(
                        context,
                        L"Could not update a pending account like.",
                        L"like-attempt");
                    co_return false;
                }

                co_await m_remoteMusic.SetAccountLikedAsync(
                    context,
                    TrackDto(like.Track),
                    like.DesiredState,
                    winrt::hstring(like.RemoteTrackId));
                if (!m_remoteMusic.IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }
                if (!m_database.AcknowledgePendingLike(
                    accountId,
                    like.RemoteTrackId,
                    like.DesiredState))
                {
                    FailSync(
                        context,
                        L"Could not acknowledge a synchronized account like.",
                        L"like-acknowledgement");
                    co_return false;
                }
            }

            auto payload = co_await m_remoteMusic.GetAccountLibraryAsync(context);
            if (!m_remoteMusic.IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }

            auto synchronizedAt = UtcNow();
            auto parsed = ParseLibrary(payload, context.OwnerId(), profile, synchronizedAt);
            if (!parsed.Valid || parsed.Snapshot.Profile.AccountId != accountId)
            {
                FailSync(
                    context,
                    L"The account library response was not accepted.",
                    L"library-response");
                co_return false;
            }
            if (!m_remoteMusic.IsCurrent(context))
            {
                throw winrt::hresult_canceled();
            }
            if (!m_database.ReplaceAccountLibrary(accountId, parsed.Snapshot))
            {
                FailSync(
                    context,
                    L"Could not save the synchronized account library.",
                    L"cache-write");
                co_return false;
            }
            if (!m_remoteMusic.IsCurrent(context))
            {
                co_return false;
            }

            SetSafeError({});
            co_return true;
        }
        catch (winrt::hresult_canceled const&)
        {
            if (operation && m_remoteMusic.IsCurrent(*operation))
            {
                FailSync(*operation, L"Account synchronization was canceled.", L"canceled");
            }
        }
        catch (winrt::hresult_error const& error)
        {
            if (operation && m_remoteMusic.IsCurrent(*operation))
            {
                auto message = error.message();
                FailSync(
                    *operation,
                    message.empty() ? winrt::hstring{ L"Account synchronization failed." } : message,
                    IsAccountUnauthorized(error) ? L"unauthorized" : L"request-failed");
            }
            else if (!operation)
            {
                auto message = error.message();
                SetSafeError(message.empty()
                    ? winrt::hstring{ L"Account mode is not active." }
                    : message);
            }
        }
        catch (...)
        {
            if (operation && m_remoteMusic.IsCurrent(*operation))
            {
                FailSync(*operation, L"Account synchronization failed.", L"unexpected");
            }
            else if (!operation)
            {
                SetSafeError(L"Account synchronization failed.");
            }
        }
        co_return false;
    }
}
