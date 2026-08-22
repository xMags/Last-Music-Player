#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/ProviderHelpers.h"

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;

    bool DatabaseEngine::ClearAllUserData()
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || !Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        bool ok =
            Exec(m_db, "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1;") &&
            Exec(m_db, "DELETE FROM AccountPlaylistTracks;") &&
            Exec(m_db, "DELETE FROM PlaybackEvents;") &&
            Exec(m_db, "DELETE FROM PendingLikes;") &&
            Exec(m_db, "DELETE FROM AccountSyncState;") &&
            Exec(m_db, "DELETE FROM AccountPlaylists;") &&
            Exec(m_db, "DELETE FROM AccountTracks;") &&
            Exec(m_db, "DELETE FROM AccountProfiles;") &&
            Exec(m_db, "DELETE FROM AlbumTracks;") &&
            Exec(m_db, "DELETE FROM PlaylistTracks;") &&
            Exec(m_db, "DELETE FROM Albums;") &&
            Exec(m_db, "DELETE FROM Playlists;") &&
            Exec(m_db, "DELETE FROM Tracks;");

        if (ok)
        {
            TryExec(m_db, "DELETE FROM sqlite_sequence WHERE name IN ('Tracks','Albums','Playlists');");
            ok = Exec(m_db, "COMMIT;");
        }

        if (!ok)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }

        return Exec(m_db, "PRAGMA wal_checkpoint(TRUNCATE);")
            && Exec(m_db, "VACUUM;");
    }

    void DatabaseEngine::BeginLocalScan()
    {
        std::scoped_lock lock{ m_mutex };
        // Scan completion now flips inactive rows after metadata has been read
        // and new rows have been upserted, so interrupted scans keep the old
        // local library visible on the next launch.
    }

    void DatabaseEngine::CompleteLocalScan(std::vector<std::wstring> const& activeSourceKeys)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db)
        {
            return;
        }

        if (!Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return;
        }

        if (!Exec(m_db, "UPDATE Tracks SET IsActive=0 WHERE SourceKind='local';"))
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }

        bool ok = true;
        {
            Statement stmt{ m_db, "UPDATE Tracks SET IsActive=1 WHERE SourceKind='local' AND SourceKey=?;" };
            if (!stmt)
            {
                ok = false;
            }
            else
            {
                for (auto const& key : activeSourceKeys)
                {
                    sqlite3_reset(stmt.value);
                    sqlite3_clear_bindings(stmt.value);
                    BindText(stmt.value, 1, key);
                    if (sqlite3_step(stmt.value) != SQLITE_DONE)
                    {
                        ok = false;
                        break;
                    }
                }
            }
        }

        if (ok)
        {
            Exec(m_db, "COMMIT;");
        }
        else
        {
            TryExec(m_db, "ROLLBACK;");
        }
    }

    int64_t DatabaseEngine::UpsertLocalTrack(TrackInfo const& track, std::wstring const& sourceKey)
    {
        std::scoped_lock lock{ m_mutex };
        return UpsertTrack(track, L"local", L"", sourceKey, true);
    }

    int64_t DatabaseEngine::UpsertRemoteTrack(TrackInfo const& track, std::wstring const& sourceKey)
    {
        std::scoped_lock lock{ m_mutex };
        auto provider = std::wstring(track.Provider().c_str());
        if (provider.empty())
        {
            provider = L"remote";
        }
        return UpsertTrack(track, L"remote", provider, sourceKey, true);
    }

    int64_t DatabaseEngine::UpsertTrack(TrackInfo const& track, std::wstring const& sourceKind, std::wstring const& provider, std::wstring const& sourceKey, bool active)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty())
        {
            return 0;
        }

        static constexpr char kSql[] =
            "INSERT INTO Tracks (SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsActive, RemoteId, UpdatedAt) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now')) "
            "ON CONFLICT(SourceKey) DO UPDATE SET "
            "SourceKind=excluded.SourceKind, Provider=excluded.Provider, SourceUrl=excluded.SourceUrl, FilePath=excluded.FilePath, "
            "Title=excluded.Title, Artist=excluded.Artist, Album=excluded.Album, Genre=excluded.Genre, DurationSeconds=excluded.DurationSeconds, "
            "ArtworkUrl=excluded.ArtworkUrl, DateAddedSortKey=excluded.DateAddedSortKey, DateAddedText=excluded.DateAddedText, DurationText=excluded.DurationText, "
            "IsActive=excluded.IsActive, RemoteId=excluded.RemoteId, "
            "UpdatedAt=excluded.UpdatedAt;";

        Statement stmt{ m_db, kSql };
        if (!stmt)
        {
            return 0;
        }

        auto sourceUrl = std::wstring(track.SourceUrl().c_str());
        if (sourceUrl.empty() && sourceKind == L"remote")
        {
            sourceUrl = std::wstring(track.FilePath().c_str());
        }
        if (sourceKind == L"remote"
            && !IsSafeRemoteUrl(winrt::hstring(sourceUrl), RemoteUrlUse::Durable))
        {
            sourceUrl.clear();
        }

        BindText(stmt.value, 1, sourceKey);
        BindText(stmt.value, 2, sourceKind);
        BindText(stmt.value, 3, provider);
        BindText(stmt.value, 4, sourceUrl);
        BindText(stmt.value, 5, StoredFilePathFor(track, sourceKind, sourceKey));
        BindText(stmt.value, 6, std::wstring(track.Title().c_str()));
        BindText(stmt.value, 7, std::wstring(track.Artist().c_str()));
        BindText(stmt.value, 8, std::wstring(track.Album().c_str()));
        BindText(stmt.value, 9, std::wstring(track.Genre().c_str()));
        sqlite3_bind_double(stmt.value, 10, track.DurationSeconds());
        auto artworkUrl = std::wstring(track.ArtworkUrl().c_str());
        if (sourceKind == L"remote"
            && !IsSafeRemoteUrl(winrt::hstring(artworkUrl), RemoteUrlUse::Durable))
        {
            artworkUrl.clear();
        }
        BindText(stmt.value, 11, artworkUrl);
        sqlite3_bind_double(stmt.value, 12, track.DateAddedSortKey());
        BindText(stmt.value, 13, std::wstring(track.DateAdded().c_str()));
        BindText(stmt.value, 14, std::wstring(track.Duration().c_str()));
        sqlite3_bind_int(stmt.value, 15, active ? 1 : 0);
        BindText(stmt.value, 16, std::wstring(track.RemoteId().c_str()));

        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            ::OutputDebugStringA(sqlite3_errmsg(m_db));
            return 0;
        }

        Statement lookup{ m_db, "SELECT Id FROM Tracks WHERE SourceKey=?;" };
        if (!lookup)
        {
            return 0;
        }
        BindText(lookup.value, 1, sourceKey);
        if (sqlite3_step(lookup.value) == SQLITE_ROW)
        {
            return sqlite3_column_int64(lookup.value, 0);
        }
        return sqlite3_last_insert_rowid(m_db);
    }

    void DatabaseEngine::RecordPlayback(std::wstring const& sourceKey, uint64_t playOrder)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty())
        {
            return;
        }

        Statement stmt{ m_db, "UPDATE Tracks SET PlayCount=COALESCE(PlayCount,0)+1, LastPlayedOrder=?, LastPlayed=datetime('now'), LastPlayedExact=1 WHERE SourceKey=?;" };
        if (!stmt) return;
        sqlite3_bind_int64(stmt.value, 1, static_cast<sqlite3_int64>(playOrder));
        BindText(stmt.value, 2, sourceKey);
        // Surface failures so play-count loss isn't silent under
        // contention (SQLITE_BUSY past the 5s timeout, constraint
        // violations, etc.). Matches the !=SQLITE_DONE pattern used
        // elsewhere in this file.
        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            return;
        }
    }

    void DatabaseEngine::RecordPlaybackPosition(std::wstring const& sourceKey, double positionSeconds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty())
        {
            return;
        }

        Statement stmt{ m_db, "UPDATE Tracks SET LastPositionSeconds=? WHERE SourceKey=?;" };
        if (!stmt) return;
        sqlite3_bind_double(stmt.value, 1, positionSeconds > 0.0 ? positionSeconds : 0.0);
        BindText(stmt.value, 2, sourceKey);
        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            return;
        }
    }

    void DatabaseEngine::MergePlaybackStats(std::wstring const& sourceKey, uint32_t playCount, uint64_t lastPlayedOrder)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty() || (playCount == 0 && lastPlayedOrder == 0))
        {
            return;
        }

        Statement stmt{ m_db,
            "UPDATE Tracks SET "
            "PlayCount=MAX(COALESCE(PlayCount,0), ?), "
            "LastPlayedOrder=MAX(COALESCE(LastPlayedOrder,0), ?), "
            "LastPlayed=CASE WHEN ? > COALESCE(LastPlayedOrder,0) THEN datetime('now') ELSE LastPlayed END "
            "WHERE SourceKey=?;" };
        if (!stmt) return;
        sqlite3_bind_int(stmt.value, 1, static_cast<int>(playCount));
        sqlite3_bind_int64(stmt.value, 2, static_cast<sqlite3_int64>(lastPlayedOrder));
        sqlite3_bind_int64(stmt.value, 3, static_cast<sqlite3_int64>(lastPlayedOrder));
        BindText(stmt.value, 4, sourceKey);
        // Check return code so a SQLITE_BUSY / SQLITE_CONSTRAINT
        // doesn't silently drop the imported stats merge.
        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            return;
        }
    }

    uint64_t DatabaseEngine::MaxLastPlayedOrder() const
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db)
        {
            return 0;
        }

        Statement stmt{ m_db, "SELECT COALESCE(MAX(LastPlayedOrder),0) FROM Tracks;" };
        if (!stmt)
        {
            return 0;
        }

        if (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto value = sqlite3_column_int64(stmt.value, 0);
            return value > 0 ? static_cast<uint64_t>(value) : 0;
        }
        return 0;
    }

    void DatabaseEngine::SetLiked(std::wstring const& sourceKey, bool liked)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty())
        {
            return;
        }

        Statement stmt{ m_db, "UPDATE Tracks SET IsLiked=? WHERE SourceKey=?;" };
        if (!stmt) return;
        sqlite3_bind_int(stmt.value, 1, liked ? 1 : 0);
        BindText(stmt.value, 2, sourceKey);
        // Surface failures so a like-toggle isn't silently lost under
        // DB contention. Matches the !=SQLITE_DONE pattern elsewhere
        // in this file.
        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            return;
        }
    }

    bool DatabaseEngine::IsLiked(std::wstring const& sourceKey) const
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || sourceKey.empty())
        {
            return false;
        }

        Statement stmt{ m_db, "SELECT IsLiked FROM Tracks WHERE SourceKey=?;" };
        if (!stmt) return false;
        BindText(stmt.value, 1, sourceKey);
        return sqlite3_step(stmt.value) == SQLITE_ROW && sqlite3_column_int(stmt.value, 0) != 0;
    }
}
