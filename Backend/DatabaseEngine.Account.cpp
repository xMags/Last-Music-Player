#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <unordered_set>
#include <winrt/Windows.Data.Json.h>

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;
    using namespace winrt::Windows::Data::Json;

    namespace
    {
        std::wstring SanitizeAccountUrl(winrt::hstring const& value)
        {
            auto cleaned = RemoveLegacyProviderUrlCredential(value);
            return IsSafeRemoteUrl(cleaned, RemoteUrlUse::Durable)
                ? std::wstring(cleaned.c_str())
                : std::wstring{};
        }

        void InsertString(JsonObject const& object, wchar_t const* key, winrt::hstring const& value)
        {
            object.Insert(key, JsonValue::CreateStringValue(value));
        }

        std::wstring SerializeTrack(TrackInfo const& track)
        {
            if (!track)
            {
                return L"{}";
            }

            JsonObject object;
            InsertString(object, L"remoteId", track.RemoteId());
            InsertString(object, L"sourceUrl", winrt::hstring(SanitizeAccountUrl(track.SourceUrl())));
            InsertString(object, L"title", track.Title());
            InsertString(object, L"artist", track.Artist());
            InsertString(object, L"album", track.Album());
            InsertString(object, L"genre", track.Genre());
            InsertString(object, L"artworkUrl", winrt::hstring(SanitizeAccountUrl(track.ArtworkUrl())));
            InsertString(object, L"dateAdded", track.DateAdded());
            InsertString(object, L"duration", track.Duration());
            object.Insert(L"durationSeconds", JsonValue::CreateNumberValue(track.DurationSeconds()));
            object.Insert(L"dateAddedSortKey", JsonValue::CreateNumberValue(track.DateAddedSortKey()));
            object.Insert(L"isLiked", JsonValue::CreateBooleanValue(track.IsLiked()));
            return std::wstring(object.Stringify().c_str());
        }

        TrackInfo DeserializeTrack(std::wstring const& payload)
        {
            JsonObject object;
            if (!JsonObject::TryParse(winrt::hstring(payload), object))
            {
                return nullptr;
            }

            TrackInfo track;
            track.RemoteId(object.GetNamedString(L"remoteId", L""));
            track.SourceUrl(object.GetNamedString(L"sourceUrl", L""));
            track.Title(object.GetNamedString(L"title", L""));
            track.Artist(object.GetNamedString(L"artist", L""));
            track.Album(object.GetNamedString(L"album", L""));
            track.Genre(object.GetNamedString(L"genre", L""));
            track.ArtworkUrl(object.GetNamedString(L"artworkUrl", L""));
            track.DateAdded(object.GetNamedString(L"dateAdded", L""));
            track.Duration(object.GetNamedString(L"duration", L""));
            track.DurationSeconds(object.GetNamedNumber(L"durationSeconds", 0.0));
            track.DateAddedSortKey(object.GetNamedNumber(L"dateAddedSortKey", 0.0));
            track.IsLiked(object.GetNamedBoolean(L"isLiked", false));
            track.SourceKind(L"remote");
            track.Provider(L"account");
            track.SourceLabel(L"Account");
            track.FilePath(L"");
            return track;
        }

        TrackInfo AccountTrackFromStatement(sqlite3_stmt* stmt, int firstColumn = 0)
        {
            TrackInfo track;
            track.CatalogId(-sqlite3_column_int64(stmt, firstColumn));
            track.RemoteId(winrt::hstring(ColumnText(stmt, firstColumn + 1)));
            track.SourceUrl(winrt::hstring(ColumnText(stmt, firstColumn + 2)));
            track.Title(winrt::hstring(ColumnText(stmt, firstColumn + 3)));
            track.Artist(winrt::hstring(ColumnText(stmt, firstColumn + 4)));
            track.Album(winrt::hstring(ColumnText(stmt, firstColumn + 5)));
            track.Genre(winrt::hstring(ColumnText(stmt, firstColumn + 6)));
            track.DurationSeconds(sqlite3_column_double(stmt, firstColumn + 7));
            track.ArtworkUrl(winrt::hstring(ColumnText(stmt, firstColumn + 8)));
            track.DateAddedSortKey(sqlite3_column_double(stmt, firstColumn + 9));
            track.DateAdded(winrt::hstring(ColumnText(stmt, firstColumn + 10)));
            track.Duration(winrt::hstring(ColumnText(stmt, firstColumn + 11)));
            track.IsLiked(sqlite3_column_int(stmt, firstColumn + 12) != 0);
            track.SourceKind(L"remote");
            track.Provider(L"account");
            track.SourceLabel(L"Account");
            track.FilePath(L"");
            return track;
        }

        bool EnsureAccountProfile(sqlite3* db, std::wstring const& accountId)
        {
            Statement stmt{ db,
                "INSERT INTO AccountProfiles (AccountId, UpdatedAtUtc) VALUES (?, datetime('now')) "
                "ON CONFLICT(AccountId) DO NOTHING;" };
            if (!stmt)
            {
                return false;
            }
            BindText(stmt.value, 1, accountId);
            return sqlite3_step(stmt.value) == SQLITE_DONE;
        }
        bool UpsertAccountTrackMetadata(sqlite3* db, std::wstring const& accountId, TrackInfo const& track)
        {
            if (!track || accountId.empty() || track.RemoteId().empty())
            {
                return false;
            }

            auto sourceUrl = SanitizeAccountUrl(track.SourceUrl());
            if (sourceUrl.empty() || !EnsureAccountProfile(db, accountId))
            {
                return false;
            }

            Statement stmt{ db,
                "INSERT INTO AccountTracks (AccountId, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
                "DateAddedSortKey, DateAddedText, DurationText, IsLiked, UpdatedAtUtc) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now')) "
                "ON CONFLICT(AccountId, RemoteId) DO UPDATE SET "
                "SourceUrl=excluded.SourceUrl, "
                "Title=CASE WHEN excluded.Title<>'' THEN excluded.Title ELSE AccountTracks.Title END, "
                "Artist=CASE WHEN excluded.Artist<>'' THEN excluded.Artist ELSE AccountTracks.Artist END, "
                "Album=CASE WHEN excluded.Album<>'' THEN excluded.Album ELSE AccountTracks.Album END, "
                "Genre=CASE WHEN excluded.Genre<>'' THEN excluded.Genre ELSE AccountTracks.Genre END, "
                "DurationSeconds=CASE WHEN excluded.DurationSeconds>0 THEN excluded.DurationSeconds ELSE AccountTracks.DurationSeconds END, "
                "ArtworkUrl=CASE WHEN excluded.ArtworkUrl<>'' THEN excluded.ArtworkUrl ELSE AccountTracks.ArtworkUrl END, "
                "DateAddedSortKey=CASE WHEN excluded.DateAddedSortKey>0 THEN excluded.DateAddedSortKey ELSE AccountTracks.DateAddedSortKey END, "
                "DateAddedText=CASE WHEN excluded.DateAddedText<>'' THEN excluded.DateAddedText ELSE AccountTracks.DateAddedText END, "
                "DurationText=CASE WHEN excluded.DurationText<>'' THEN excluded.DurationText ELSE AccountTracks.DurationText END, "
                "UpdatedAtUtc=datetime('now');" };
            if (!stmt)
            {
                return false;
            }

            BindText(stmt.value, 1, accountId);
            BindText(stmt.value, 2, std::wstring(track.RemoteId().c_str()));
            BindText(stmt.value, 3, sourceUrl);
            BindText(stmt.value, 4, std::wstring(track.Title().c_str()));
            BindText(stmt.value, 5, std::wstring(track.Artist().c_str()));
            BindText(stmt.value, 6, std::wstring(track.Album().c_str()));
            BindText(stmt.value, 7, std::wstring(track.Genre().c_str()));
            sqlite3_bind_double(stmt.value, 8, track.DurationSeconds());
            BindText(stmt.value, 9, SanitizeAccountUrl(track.ArtworkUrl()));
            sqlite3_bind_double(stmt.value, 10, track.DateAddedSortKey());
            BindText(stmt.value, 11, std::wstring(track.DateAdded().c_str()));
            BindText(stmt.value, 12, std::wstring(track.Duration().c_str()));
            sqlite3_bind_int(stmt.value, 13, track.IsLiked() ? 1 : 0);
            return sqlite3_step(stmt.value) == SQLITE_DONE;
        }


        bool CommitOrRollback(sqlite3* db)
        {
            if (Exec(db, "COMMIT;"))
            {
                return true;
            }
            TryExec(db, "ROLLBACK;");
            return false;
        }
    }

    bool DatabaseEngine::SetRemoteLibraryContext(std::wstring const& mode, std::wstring const& accountId)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || (mode != L"LocalOnly" && mode != L"ApiKey" && mode != L"Account"))
        {
            return false;
        }

        auto ownerId = mode == L"Account" ? accountId : std::wstring{};
        if (mode == L"Account" && (ownerId.empty() || !EnsureAccountProfile(m_db, ownerId)))
        {
            return false;
        }

        Statement stmt{ m_db,
            "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, ?, ?) "
            "ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode=excluded.RemoteMode, AccountId=excluded.AccountId;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, mode);
        BindText(stmt.value, 2, ownerId);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    std::wstring DatabaseEngine::ActiveAccountId() const
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db)
        {
            return {};
        }

        Statement stmt{ m_db, "SELECT AccountId FROM ActiveAccountContext WHERE SingletonId=1 AND RemoteMode='Account';" };
        if (stmt && sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            return ColumnText(stmt.value, 0);
        }
        return {};
    }

    bool DatabaseEngine::ReplaceAccountLibrary(
        std::wstring const& expectedAccountId,
        AccountLibrarySnapshot const& snapshot)
    {
        std::scoped_lock lock{ m_mutex };
        auto const& accountId = snapshot.Profile.AccountId;
        if (!m_db || expectedAccountId.empty() || accountId != expectedAccountId)
        {
            return false;
        }

        Statement activeContext{
            m_db,
            "SELECT AccountId FROM ActiveAccountContext WHERE SingletonId=1 AND RemoteMode='Account';"
        };
        if (!activeContext || sqlite3_step(activeContext.value) != SQLITE_ROW
            || ColumnText(activeContext.value, 0) != expectedAccountId
            || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        auto fail = [&]()
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        };

        Statement profile{ m_db,
            "INSERT INTO AccountProfiles (AccountId, DisplayName, Username, AvatarUrl, PlanLabel, UpdatedAtUtc) "
            "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(AccountId) DO UPDATE SET "
            "DisplayName=excluded.DisplayName, Username=excluded.Username, AvatarUrl=excluded.AvatarUrl, "
            "PlanLabel=excluded.PlanLabel, UpdatedAtUtc=excluded.UpdatedAtUtc;" };
        if (!profile)
        {
            return fail();
        }
        BindText(profile.value, 1, accountId);
        BindText(profile.value, 2, snapshot.Profile.DisplayName);
        BindText(profile.value, 3, snapshot.Profile.Username);
        BindText(profile.value, 4, SanitizeAccountUrl(winrt::hstring(snapshot.Profile.AvatarUrl)));
        BindText(profile.value, 5, snapshot.Profile.PlanLabel);
        BindText(profile.value, 6, snapshot.Profile.UpdatedAtUtc);
        if (sqlite3_step(profile.value) != SQLITE_DONE)
        {
            return fail();
        }

        for (auto table : { "AccountPlaylistTracks", "AccountPlaylists", "AccountTracks" })
        {
            auto sql = std::string("DELETE FROM ") + table + " WHERE AccountId=?;";
            Statement remove{ m_db, sql.c_str() };
            if (!remove)
            {
                return fail();
            }
            BindText(remove.value, 1, accountId);
            if (sqlite3_step(remove.value) != SQLITE_DONE)
            {
                return fail();
            }
        }

        Statement insertTrack{ m_db,
            "INSERT INTO AccountTracks (AccountId, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
            "DateAddedSortKey, DateAddedText, DurationText, IsLiked, PlayCount, FirstPlayedAtUtc, LastPlayedAtUtc, LastPositionSeconds, UpdatedAtUtc) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);" };
        if (!insertTrack)
        {
            return fail();
        }

        std::unordered_set<std::wstring> insertedTrackIds;
        for (auto const& entry : snapshot.Tracks)
        {
            auto track = entry.Track;
            if (!track)
            {
                continue;
            }
            auto remoteId = std::wstring(track.RemoteId().c_str());
            if (remoteId.empty() || !insertedTrackIds.insert(remoteId).second)
            {
                continue;
            }

            sqlite3_reset(insertTrack.value);
            sqlite3_clear_bindings(insertTrack.value);
            BindText(insertTrack.value, 1, accountId);
            BindText(insertTrack.value, 2, remoteId);
            BindText(insertTrack.value, 3, SanitizeAccountUrl(track.SourceUrl()));
            BindText(insertTrack.value, 4, std::wstring(track.Title().c_str()));
            BindText(insertTrack.value, 5, std::wstring(track.Artist().c_str()));
            BindText(insertTrack.value, 6, std::wstring(track.Album().c_str()));
            BindText(insertTrack.value, 7, std::wstring(track.Genre().c_str()));
            sqlite3_bind_double(insertTrack.value, 8, track.DurationSeconds());
            BindText(insertTrack.value, 9, SanitizeAccountUrl(track.ArtworkUrl()));
            sqlite3_bind_double(insertTrack.value, 10, track.DateAddedSortKey());
            BindText(insertTrack.value, 11, std::wstring(track.DateAdded().c_str()));
            BindText(insertTrack.value, 12, std::wstring(track.Duration().c_str()));
            sqlite3_bind_int(insertTrack.value, 13, track.IsLiked() ? 1 : 0);
            sqlite3_bind_int64(insertTrack.value, 14, entry.PlayCount);
            BindText(insertTrack.value, 15, entry.FirstPlayedAtUtc);
            BindText(insertTrack.value, 16, entry.LastPlayedAtUtc);
            sqlite3_bind_double(insertTrack.value, 17, entry.LastPositionSeconds);
            BindText(insertTrack.value, 18, snapshot.SynchronizedAtUtc);
            if (sqlite3_step(insertTrack.value) != SQLITE_DONE)
            {
                return fail();
            }
        }

        Statement insertPlaylist{ m_db,
            "INSERT INTO AccountPlaylists (AccountId, PlaylistId, Name, Description, SourceUrl, UpdatedAtUtc) "
            "VALUES (?, ?, ?, ?, ?, ?);" };
        Statement insertPlaylistTrack{ m_db,
            "INSERT OR IGNORE INTO AccountPlaylistTracks (AccountId, PlaylistId, RemoteId, TrackOrder) VALUES (?, ?, ?, ?);" };
        if (!insertPlaylist || !insertPlaylistTrack)
        {
            return fail();
        }

        std::unordered_set<std::wstring> insertedPlaylistIds;
        for (auto const& playlist : snapshot.Playlists)
        {
            if (playlist.PlaylistId.empty() || !insertedPlaylistIds.insert(playlist.PlaylistId).second)
            {
                continue;
            }
            sqlite3_reset(insertPlaylist.value);
            sqlite3_clear_bindings(insertPlaylist.value);
            BindText(insertPlaylist.value, 1, accountId);
            BindText(insertPlaylist.value, 2, playlist.PlaylistId);
            BindText(insertPlaylist.value, 3, playlist.Name);
            BindText(insertPlaylist.value, 4, playlist.Description);
            BindText(insertPlaylist.value, 5, SanitizeAccountUrl(winrt::hstring(playlist.SourceUrl)));
            BindText(insertPlaylist.value, 6, playlist.UpdatedAtUtc);
            if (sqlite3_step(insertPlaylist.value) != SQLITE_DONE)
            {
                return fail();
            }

            int order = 0;
            for (auto const& remoteId : playlist.RemoteTrackIds)
            {
                if (!insertedTrackIds.contains(remoteId))
                {
                    continue;
                }
                sqlite3_reset(insertPlaylistTrack.value);
                sqlite3_clear_bindings(insertPlaylistTrack.value);
                BindText(insertPlaylistTrack.value, 1, accountId);
                BindText(insertPlaylistTrack.value, 2, playlist.PlaylistId);
                BindText(insertPlaylistTrack.value, 3, remoteId);
                sqlite3_bind_int(insertPlaylistTrack.value, 4, order++);
                if (sqlite3_step(insertPlaylistTrack.value) != SQLITE_DONE)
                {
                    return fail();
                }
            }
        }

        AccountSyncState state;
        state.AccountId = accountId;
        state.LastSuccessfulSyncUtc = snapshot.SynchronizedAtUtc;
        auto existing = LoadAccountSyncState(accountId);
        state.LegacyHistoryImportState = existing.LegacyHistoryImportState;
        if (!SaveAccountSyncState(state))
        {
            return fail();
        }
        return CommitOrRollback(m_db);
    }

    std::optional<AccountLibrarySnapshot> DatabaseEngine::LoadAccountLibrary(std::wstring const& accountId) const
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty())
        {
            return std::nullopt;
        }

        AccountLibrarySnapshot snapshot;
        Statement profile{ m_db,
            "SELECT AccountId, DisplayName, Username, AvatarUrl, PlanLabel, UpdatedAtUtc FROM AccountProfiles WHERE AccountId=?;" };
        if (!profile)
        {
            return std::nullopt;
        }
        BindText(profile.value, 1, accountId);
        if (sqlite3_step(profile.value) != SQLITE_ROW)
        {
            return std::nullopt;
        }
        snapshot.Profile.AccountId = ColumnText(profile.value, 0);
        snapshot.Profile.DisplayName = ColumnText(profile.value, 1);
        snapshot.Profile.Username = ColumnText(profile.value, 2);
        snapshot.Profile.AvatarUrl = ColumnText(profile.value, 3);
        snapshot.Profile.PlanLabel = ColumnText(profile.value, 4);
        snapshot.Profile.UpdatedAtUtc = ColumnText(profile.value, 5);

        Statement tracks{ m_db,
            "SELECT rowid, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, "
            "DateAddedText, DurationText, IsLiked, PlayCount, FirstPlayedAtUtc, LastPlayedAtUtc, LastPositionSeconds, UpdatedAtUtc "
            "FROM AccountTracks WHERE AccountId=? ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC;" };
        if (!tracks)
        {
            return std::nullopt;
        }
        BindText(tracks.value, 1, accountId);
        while (sqlite3_step(tracks.value) == SQLITE_ROW)
        {
            AccountTrackSnapshot entry;
            entry.Track = AccountTrackFromStatement(tracks.value);
            entry.PlayCount = sqlite3_column_int64(tracks.value, 13);
            entry.FirstPlayedAtUtc = ColumnText(tracks.value, 14);
            entry.LastPlayedAtUtc = ColumnText(tracks.value, 15);
            entry.LastPositionSeconds = sqlite3_column_double(tracks.value, 16);
            snapshot.SynchronizedAtUtc = (std::max)(snapshot.SynchronizedAtUtc, ColumnText(tracks.value, 17));
            snapshot.Tracks.push_back(std::move(entry));
        }

        Statement playlists{ m_db,
            "SELECT PlaylistId, Name, Description, SourceUrl, UpdatedAtUtc FROM AccountPlaylists WHERE AccountId=? "
            "ORDER BY UpdatedAtUtc DESC, Name COLLATE NOCASE ASC;" };
        if (!playlists)
        {
            return std::nullopt;
        }
        BindText(playlists.value, 1, accountId);
        while (sqlite3_step(playlists.value) == SQLITE_ROW)
        {
            AccountPlaylistSnapshot playlist;
            playlist.PlaylistId = ColumnText(playlists.value, 0);
            playlist.Name = ColumnText(playlists.value, 1);
            playlist.Description = ColumnText(playlists.value, 2);
            playlist.SourceUrl = ColumnText(playlists.value, 3);
            playlist.UpdatedAtUtc = ColumnText(playlists.value, 4);

            Statement ids{ m_db,
                "SELECT RemoteId FROM AccountPlaylistTracks WHERE AccountId=? AND PlaylistId=? ORDER BY TrackOrder;" };
            if (!ids)
            {
                return std::nullopt;
            }
            BindText(ids.value, 1, accountId);
            BindText(ids.value, 2, playlist.PlaylistId);
            while (sqlite3_step(ids.value) == SQLITE_ROW)
            {
                playlist.RemoteTrackIds.push_back(ColumnText(ids.value, 0));
            }
            snapshot.Playlists.push_back(std::move(playlist));
        }

        auto state = LoadAccountSyncState(accountId);
        if (!state.LastSuccessfulSyncUtc.empty())
        {
            snapshot.SynchronizedAtUtc = state.LastSuccessfulSyncUtc;
        }
        return snapshot;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadAccountPlaylistTracks(
        std::wstring const& accountId,
        std::wstring const& playlistId) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> result;
        if (!m_db || accountId.empty() || playlistId.empty())
        {
            return result;
        }

        Statement stmt{ m_db,
            "SELECT t.rowid, t.RemoteId, t.SourceUrl, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, "
            "t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked "
            "FROM AccountPlaylistTracks pt JOIN AccountTracks t ON t.AccountId=pt.AccountId AND t.RemoteId=pt.RemoteId "
            "WHERE pt.AccountId=? AND pt.PlaylistId=? ORDER BY pt.TrackOrder, t.Title COLLATE NOCASE;" };
        if (!stmt)
        {
            return result;
        }
        BindText(stmt.value, 1, accountId);
        BindText(stmt.value, 2, playlistId);
        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = AccountTrackFromStatement(stmt.value);
            track.Index(index++);
            result.push_back(track);
        }
        return result;
    }

    bool DatabaseEngine::EnsureAccountTrack(std::wstring const& accountId, TrackInfo const& track)
    {
        std::scoped_lock lock{ m_mutex };
        return m_db && UpsertAccountTrackMetadata(m_db, accountId, track);
    }

    bool DatabaseEngine::RecordAccountPlayback(
        std::wstring const& accountId,
        TrackInfo const& track,
        double positionSeconds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || !track || accountId.empty() || track.RemoteId().empty()
            || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        if (!UpsertAccountTrackMetadata(m_db, accountId, track))
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }

        Statement stmt{ m_db,
            "UPDATE AccountTracks SET "
            "PlayCount=COALESCE(PlayCount,0)+1, "
            "FirstPlayedAtUtc=COALESCE(NULLIF(FirstPlayedAtUtc,''),datetime('now')), "
            "LastPlayedAtUtc=datetime('now'), LastPositionSeconds=?, UpdatedAtUtc=datetime('now') "
            "WHERE AccountId=? AND RemoteId=?;" };
        if (!stmt)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        sqlite3_bind_double(stmt.value, 1, (std::max)(0.0, positionSeconds));
        BindText(stmt.value, 2, accountId);
        BindText(stmt.value, 3, std::wstring(track.RemoteId().c_str()));
        if (sqlite3_step(stmt.value) != SQLITE_DONE || sqlite3_changes(m_db) != 1)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        return CommitOrRollback(m_db);
    }

    bool DatabaseEngine::EnqueuePlaybackEvent(PlaybackEventRecord const& event)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || event.EventId.empty() || event.AccountId.empty() || event.RemoteTrackId.empty() || !event.Track)
        {
            return false;
        }
        if (!EnsureAccountProfile(m_db, event.AccountId))
        {
            return false;
        }

        Statement stmt{ m_db,
            "INSERT OR IGNORE INTO PlaybackEvents (EventId, AccountId, RemoteId, TrackJson, PlayedAtUtc, PositionSeconds) "
            "VALUES (?, ?, ?, ?, ?, ?);" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, event.EventId);
        BindText(stmt.value, 2, event.AccountId);
        BindText(stmt.value, 3, event.RemoteTrackId);
        BindText(stmt.value, 4, SerializeTrack(event.Track));
        BindText(stmt.value, 5, event.PlayedAtUtc);
        sqlite3_bind_double(stmt.value, 6, event.PositionSeconds);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    std::vector<PlaybackEventRecord> DatabaseEngine::LoadPendingPlaybackEvents(std::wstring const& accountId, int limit) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<PlaybackEventRecord> result;
        if (!m_db || accountId.empty() || limit <= 0)
        {
            return result;
        }

        Statement stmt{ m_db,
            "SELECT EventId, AccountId, RemoteId, TrackJson, PlayedAtUtc, PositionSeconds, AttemptCount "
            "FROM PlaybackEvents WHERE AccountId=? ORDER BY PlayedAtUtc, EventId LIMIT ?;" };
        if (!stmt)
        {
            return result;
        }
        BindText(stmt.value, 1, accountId);
        sqlite3_bind_int(stmt.value, 2, limit);
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            PlaybackEventRecord event;
            event.EventId = ColumnText(stmt.value, 0);
            event.AccountId = ColumnText(stmt.value, 1);
            event.RemoteTrackId = ColumnText(stmt.value, 2);
            event.Track = DeserializeTrack(ColumnText(stmt.value, 3));
            event.PlayedAtUtc = ColumnText(stmt.value, 4);
            event.PositionSeconds = sqlite3_column_double(stmt.value, 5);
            event.AttemptCount = sqlite3_column_int(stmt.value, 6);
            if (event.Track)
            {
                result.push_back(std::move(event));
            }
        }
        return result;
    }

    bool DatabaseEngine::MarkPlaybackEventsAttempted(std::wstring const& accountId, std::vector<std::wstring> const& eventIds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }
        Statement stmt{ m_db,
            "UPDATE PlaybackEvents SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now') WHERE AccountId=? AND EventId=?;" };
        if (!stmt)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        for (auto const& id : eventIds)
        {
            sqlite3_reset(stmt.value);
            sqlite3_clear_bindings(stmt.value);
            BindText(stmt.value, 1, accountId);
            BindText(stmt.value, 2, id);
            if (sqlite3_step(stmt.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
        }
        return CommitOrRollback(m_db);
    }

    bool DatabaseEngine::AcknowledgePlaybackEvents(std::wstring const& accountId, std::vector<std::wstring> const& eventIds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }
        Statement stmt{ m_db, "DELETE FROM PlaybackEvents WHERE AccountId=? AND EventId=?;" };
        if (!stmt)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        for (auto const& id : eventIds)
        {
            sqlite3_reset(stmt.value);
            sqlite3_clear_bindings(stmt.value);
            BindText(stmt.value, 1, accountId);
            BindText(stmt.value, 2, id);
            if (sqlite3_step(stmt.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
        }
        return CommitOrRollback(m_db);
    }

    bool DatabaseEngine::EnqueuePendingLike(PendingLikeRecord const& like)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || like.AccountId.empty() || like.RemoteTrackId.empty() || !like.Track)
        {
            return false;
        }
        if (!EnsureAccountProfile(m_db, like.AccountId))
        {
            return false;
        }

        Statement stmt{ m_db,
            "INSERT INTO PendingLikes (AccountId, RemoteId, DesiredState, TrackJson, UpdatedAtUtc, AttemptCount, LastAttemptAtUtc) "
            "VALUES (?, ?, ?, ?, ?, 0, NULL) ON CONFLICT(AccountId, RemoteId) DO UPDATE SET "
            "DesiredState=excluded.DesiredState, TrackJson=excluded.TrackJson, UpdatedAtUtc=excluded.UpdatedAtUtc, AttemptCount=0, LastAttemptAtUtc=NULL;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, like.AccountId);
        BindText(stmt.value, 2, like.RemoteTrackId);
        sqlite3_bind_int(stmt.value, 3, like.DesiredState ? 1 : 0);
        BindText(stmt.value, 4, SerializeTrack(like.Track));
        BindText(stmt.value, 5, like.UpdatedAtUtc);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    std::vector<PendingLikeRecord> DatabaseEngine::LoadPendingLikes(std::wstring const& accountId, int limit) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<PendingLikeRecord> result;
        if (!m_db || accountId.empty() || limit <= 0)
        {
            return result;
        }

        Statement stmt{ m_db,
            "SELECT AccountId, RemoteId, DesiredState, TrackJson, UpdatedAtUtc, AttemptCount "
            "FROM PendingLikes WHERE AccountId=? ORDER BY UpdatedAtUtc, RemoteId LIMIT ?;" };
        if (!stmt)
        {
            return result;
        }
        BindText(stmt.value, 1, accountId);
        sqlite3_bind_int(stmt.value, 2, limit);
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            PendingLikeRecord like;
            like.AccountId = ColumnText(stmt.value, 0);
            like.RemoteTrackId = ColumnText(stmt.value, 1);
            like.DesiredState = sqlite3_column_int(stmt.value, 2) != 0;
            like.Track = DeserializeTrack(ColumnText(stmt.value, 3));
            like.UpdatedAtUtc = ColumnText(stmt.value, 4);
            like.AttemptCount = sqlite3_column_int(stmt.value, 5);
            if (like.Track)
            {
                result.push_back(std::move(like));
            }
        }
        return result;
    }

    bool DatabaseEngine::MarkPendingLikeAttempted(std::wstring const& accountId, std::wstring const& remoteTrackId)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || remoteTrackId.empty())
        {
            return false;
        }
        Statement stmt{ m_db,
            "UPDATE PendingLikes SET AttemptCount=AttemptCount+1, LastAttemptAtUtc=datetime('now') WHERE AccountId=? AND RemoteId=?;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, accountId);
        BindText(stmt.value, 2, remoteTrackId);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    bool DatabaseEngine::AcknowledgePendingLike(
        std::wstring const& accountId,
        std::wstring const& remoteTrackId,
        bool desiredState)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || remoteTrackId.empty())
        {
            return false;
        }
        Statement stmt{ m_db,
            "DELETE FROM PendingLikes WHERE AccountId=? AND RemoteId=? AND DesiredState=?;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, accountId);
        BindText(stmt.value, 2, remoteTrackId);
        sqlite3_bind_int(stmt.value, 3, desiredState ? 1 : 0);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    bool DatabaseEngine::UpdateAccountTrackLike(
        std::wstring const& accountId,
        std::wstring const& remoteTrackId,
        bool liked)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || remoteTrackId.empty())
        {
            return false;
        }
        Statement stmt{ m_db,
            "UPDATE AccountTracks SET IsLiked=?, UpdatedAtUtc=datetime('now') WHERE AccountId=? AND RemoteId=?;" };
        if (!stmt)
        {
            return false;
        }
        sqlite3_bind_int(stmt.value, 1, liked ? 1 : 0);
        BindText(stmt.value, 2, accountId);
        BindText(stmt.value, 3, remoteTrackId);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    AccountSyncState DatabaseEngine::LoadAccountSyncState(std::wstring const& accountId) const
    {
        std::scoped_lock lock{ m_mutex };
        AccountSyncState state;
        state.AccountId = accountId;
        if (!m_db || accountId.empty())
        {
            return state;
        }

        Statement stmt{ m_db,
            "SELECT LastSuccessfulSyncUtc, LastSafeErrorCode, LegacyHistoryImportState FROM AccountSyncState WHERE AccountId=?;" };
        if (!stmt)
        {
            return state;
        }
        BindText(stmt.value, 1, accountId);
        if (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            state.LastSuccessfulSyncUtc = ColumnText(stmt.value, 0);
            state.LastSafeErrorCode = ColumnText(stmt.value, 1);
            state.LegacyHistoryImportState = ColumnText(stmt.value, 2);
        }
        return state;
    }

    bool DatabaseEngine::SaveAccountSyncState(AccountSyncState const& state)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || state.AccountId.empty() || !EnsureAccountProfile(m_db, state.AccountId))
        {
            return false;
        }

        Statement stmt{ m_db,
            "INSERT INTO AccountSyncState (AccountId, LastSuccessfulSyncUtc, LastSafeErrorCode, LegacyHistoryImportState) "
            "VALUES (?, ?, ?, ?) ON CONFLICT(AccountId) DO UPDATE SET "
            "LastSuccessfulSyncUtc=excluded.LastSuccessfulSyncUtc, LastSafeErrorCode=excluded.LastSafeErrorCode, "
            "LegacyHistoryImportState=excluded.LegacyHistoryImportState;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, state.AccountId);
        BindText(stmt.value, 2, state.LastSuccessfulSyncUtc);
        BindText(stmt.value, 3, state.LastSafeErrorCode);
        BindText(stmt.value, 4, state.LegacyHistoryImportState);
        return sqlite3_step(stmt.value) == SQLITE_DONE;
    }

    bool DatabaseEngine::ClearAccountData(
        std::wstring const& accountId,
        AccountDataClearMode mode)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || accountId.empty() || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        auto fail = [&]()
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        };

        Statement clearActive{ m_db,
            "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1 AND AccountId=?;" };
        Statement clearProfile{ m_db, "DELETE FROM AccountProfiles WHERE AccountId=?;" };
        if (!clearActive || !clearProfile)
        {
            return fail();
        }
        BindText(clearActive.value, 1, accountId);
        BindText(clearProfile.value, 1, accountId);
        if (sqlite3_step(clearActive.value) != SQLITE_DONE
            || sqlite3_step(clearProfile.value) != SQLITE_DONE)
        {
            return fail();
        }

        if (mode == AccountDataClearMode::RestoreActiveContext)
        {
            if (!EnsureAccountProfile(m_db, accountId))
            {
                return fail();
            }
            Statement restoreActive{ m_db,
                "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, 'Account', ?) "
                "ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode='Account', AccountId=excluded.AccountId;" };
            if (!restoreActive)
            {
                return fail();
            }
            BindText(restoreActive.value, 1, accountId);
            if (sqlite3_step(restoreActive.value) != SQLITE_DONE)
            {
                return fail();
            }
        }

        return CommitOrRollback(m_db);
    }

    bool DatabaseEngine::ClearAllAccountData()
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }
        if (!Exec(m_db, "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1; DELETE FROM AccountProfiles;"))
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        return CommitOrRollback(m_db);
    }
}
