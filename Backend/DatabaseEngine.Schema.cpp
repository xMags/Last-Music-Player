#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/DatabaseAccountSchema.h"

#include <utility>
#include <vector>

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;

    bool DatabaseEngine::EnsureSchema()
    {
        static constexpr char kCreateTracksTableSql[] =
            "CREATE TABLE IF NOT EXISTS Tracks ("
            "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "SourceKey TEXT,"
            "SourceKind TEXT DEFAULT 'local',"
            "Provider TEXT,"
            "SourceUrl TEXT,"
            "FilePath TEXT,"
            "Title TEXT,"
            "Artist TEXT,"
            "Album TEXT,"
            "Genre TEXT,"
            "DurationSeconds REAL DEFAULT 0,"
            "ArtworkUrl TEXT,"
            "DateAddedSortKey REAL DEFAULT 0,"
            "DateAddedText TEXT,"
            "DurationText TEXT,"
            "PlayCount INTEGER DEFAULT 0,"
            "LastPlayedOrder INTEGER DEFAULT 0,"
            "IsLiked INTEGER DEFAULT 0,"
            "IsActive INTEGER DEFAULT 1,"
            "UpdatedAt INTEGER DEFAULT 0,"
            "LastPlayed DATETIME,"
            "LastPlayedExact INTEGER DEFAULT 0,"
            "RemoteId TEXT);";

        if (!Exec(m_db, kCreateTracksTableSql))
        {
            return false;
        }

        if (!Exec(m_db,
            "CREATE TABLE IF NOT EXISTS Albums ("
            "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "AlbumKey TEXT UNIQUE,"
            "Title TEXT,"
            "Artist TEXT,"
            "ArtworkUrl TEXT,"
            "SourceKind TEXT DEFAULT 'manual',"
            "SourceUrl TEXT,"
            "SourceLabel TEXT,"
            "CreatedAt INTEGER DEFAULT 0,"
            "UpdatedAt INTEGER DEFAULT 0);"))
        {
            return false;
        }

        if (!Exec(m_db,
            "CREATE TABLE IF NOT EXISTS AlbumTracks ("
            "AlbumId INTEGER NOT NULL,"
            "TrackId INTEGER NOT NULL,"
            "TrackOrder INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (AlbumId, TrackId),"
            "FOREIGN KEY (AlbumId) REFERENCES Albums(Id) ON DELETE CASCADE,"
            "FOREIGN KEY (TrackId) REFERENCES Tracks(Id) ON DELETE CASCADE);"))
        {
            return false;
        }

        if (!Exec(m_db,
            "CREATE TABLE IF NOT EXISTS Playlists ("
            "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "PlaylistKey TEXT UNIQUE,"
            "Title TEXT,"
            "Description TEXT,"
            "ArtworkUrl TEXT,"
            "SourceKind TEXT DEFAULT 'manual',"
            "Provider TEXT,"
            "SourceUrl TEXT,"
            "SourceLabel TEXT,"
            "CreatedAt INTEGER DEFAULT 0,"
            "UpdatedAt INTEGER DEFAULT 0);"))
        {
            return false;
        }

        if (!Exec(m_db,
            "CREATE TABLE IF NOT EXISTS PlaylistTracks ("
            "PlaylistId INTEGER NOT NULL,"
            "TrackId INTEGER NOT NULL,"
            "TrackOrder INTEGER NOT NULL DEFAULT 0,"
            "PRIMARY KEY (PlaylistId, TrackId),"
            "FOREIGN KEY (PlaylistId) REFERENCES Playlists(Id) ON DELETE CASCADE,"
            "FOREIGN KEY (TrackId) REFERENCES Tracks(Id) ON DELETE CASCADE);"))
        {
            return false;
        }

        if (!Exec(m_db, DatabaseAccountSchema::CreateTablesSql))
        {
            return false;
        }

        // Schema migration gate: the heavy one-time data migration below used
        // to run on EVERY launch because PRAGMA user_version was written but
        // never read (~46s startup on a real library). Run it only when the
        // DB predates kSchemaVersion. Index creation stays unconditional —
        // it is idempotent (IF NOT EXISTS) and must also cover fresh DBs.
        static constexpr int kSchemaVersion = 9;
        int userVersion = 0;
        {
            Statement versionStmt{ m_db, "PRAGMA user_version;" };
            if (versionStmt && sqlite3_step(versionStmt.value) == SQLITE_ROW)
            {
                userVersion = sqlite3_column_int(versionStmt.value, 0);
            }
        }
        const bool migrationNeeded = userVersion < kSchemaVersion;

        if (migrationNeeded && userVersion < 9)
        {
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN LastPlayedExact INTEGER DEFAULT 0;");
            // Existing LastPlayed values were written by the pre-v9 playback path.
            // Mark them exact before the separate app-state migration can merge
            // aggregate-only history using approximate migration timestamps.
            TryExec(m_db, "UPDATE Tracks SET LastPlayedExact=1 WHERE COALESCE(LastPlayed,'')<>'';");
        }

        if (migrationNeeded && userVersion < 8)
        {
            TryExec(m_db, "ALTER TABLE ActiveAccountContext ADD COLUMN RemoteMode TEXT NOT NULL DEFAULT 'LocalOnly';");
        }

        if (migrationNeeded && userVersion < 7)
        {
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN RemoteId TEXT;");
        }

        if (migrationNeeded && userVersion < 6)
        {
            // V5 to V6: normalize legacy imported-collection labels without
            // retaining a provider-specific identifier in the public client.
            TryExec(m_db, "UPDATE Albums SET Artist='Music API' WHERE SourceKind<>'manual' AND COALESCE(SourceUrl,'')<>'' AND Artist=SourceLabel;");
            TryExec(m_db, "UPDATE Albums SET SourceLabel='Music API' WHERE SourceKind<>'manual' AND COALESCE(SourceUrl,'')<>'' AND COALESCE(SourceLabel,'')<>'';");
            TryExec(m_db, "UPDATE Playlists SET Description='Music API' WHERE COALESCE(Provider,'')<>'' AND COALESCE(SourceUrl,'')<>'' AND Description=SourceLabel || ' import';");
            TryExec(m_db, "UPDATE Playlists SET SourceLabel='Music API' WHERE COALESCE(Provider,'')<>'' AND COALESCE(SourceUrl,'')<>'' AND COALESCE(SourceLabel,'')<>'';");
        }

        if (migrationNeeded && userVersion < 4)
        {
            // V3 → V4: drop the dormant ReplayGainDb column. The Normalize
            // volume feature it backed was removed; the column was harmless
            // but pure waste. TryExec swallows the failure for V2-or-older
            // DBs whose v3 ADD never ran (the V3 add line was removed).
            // SQLite 3.35+ supports ALTER TABLE DROP COLUMN natively.
            TryExec(m_db, "ALTER TABLE Tracks DROP COLUMN ReplayGainDb;");
        }

        if (migrationNeeded && userVersion < 5)
        {
            // V4 to V5: normalize legacy remote labels that could appear in Now
            // Playing or collection captions while preserving local added dates.
            TryExec(m_db, "UPDATE Tracks SET DateAddedText='Music API' WHERE SourceKind='remote';");
        }

        if (migrationNeeded && userVersion < 2)
        {
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN SourceKey TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN SourceKind TEXT DEFAULT 'local';");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN Provider TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN SourceUrl TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN Album TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN Genre TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN DurationSeconds REAL DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN ArtworkUrl TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN DateAddedSortKey REAL DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN DateAddedText TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN DurationText TEXT;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN PlayCount INTEGER DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN LastPlayedOrder INTEGER DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN LastPlayed DATETIME;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN IsLiked INTEGER DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN IsActive INTEGER DEFAULT 1;");
            TryExec(m_db, "ALTER TABLE Tracks ADD COLUMN UpdatedAt INTEGER DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN Description TEXT;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN ArtworkUrl TEXT;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN SourceKind TEXT DEFAULT 'manual';");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN Provider TEXT;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN SourceUrl TEXT;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN SourceLabel TEXT;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN CreatedAt INTEGER DEFAULT 0;");
            TryExec(m_db, "ALTER TABLE Playlists ADD COLUMN UpdatedAt INTEGER DEFAULT 0;");

            TryExec(m_db, "UPDATE Tracks SET SourceKind='local' WHERE SourceKind IS NULL OR SourceKind='';");
            TryExec(m_db, "UPDATE Tracks SET IsActive=1 WHERE IsActive IS NULL;");
            TryExec(m_db, "UPDATE Tracks SET SourceKey='local|' || lower(FilePath) WHERE (SourceKey IS NULL OR SourceKey='') AND FilePath IS NOT NULL AND FilePath <> '';");
            TryExec(m_db, "UPDATE Tracks SET SourceKey='remote|' || lower(COALESCE(NULLIF(Provider,''),'provider')) || '|' || SourceUrl WHERE SourceKind='remote' AND SourceUrl IS NOT NULL AND SourceUrl <> '';");
            TryExec(m_db, "UPDATE Tracks SET SourceKey='legacy|' || Id WHERE SourceKey IS NULL OR SourceKey='';");
            TryExec(m_db, "DELETE FROM Tracks WHERE Id NOT IN (SELECT MIN(Id) FROM Tracks GROUP BY SourceKey);");
            if (!NormalizeRemoteTrackSourceKeys(m_db))
            {
                return false;
            }
            if (TracksTableHasLegacyFilePathUnique(m_db) && !RebuildTracksTableWithoutFilePathUnique(m_db))
            {
                return false;
            }
        }
        // Account playback belongs in AccountTracks. Remove rows written by
        // pre-cutover desktop builds so API-key mode cannot expose another
        // account's cached playback.
        if (!Exec(m_db, DatabaseAccountSchema::RemoveLegacyGenericAccountTracksSql))
        {
            return false;
        }
        if (!Exec(m_db, DatabaseAccountSchema::RemoveTransientRemoteUrlsSql))
        {
            return false;
        }



        if (!Exec(m_db, "CREATE UNIQUE INDEX IF NOT EXISTS IX_Tracks_SourceKey ON Tracks(SourceKey);"))
        {
            return false;
        }
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_SourceKind_Active ON Tracks(SourceKind, IsActive);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_Album ON Tracks(Album COLLATE NOCASE);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_Artist ON Tracks(Artist COLLATE NOCASE);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_Genre ON Tracks(Genre COLLATE NOCASE);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_DateAdded ON Tracks(DateAddedSortKey DESC);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AlbumTracks_TrackId ON AlbumTracks(TrackId);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AlbumTracks_Order ON AlbumTracks(AlbumId, TrackOrder);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_PlaylistTracks_TrackId ON PlaylistTracks(TrackId);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_PlaylistTracks_Order ON PlaylistTracks(PlaylistId, TrackOrder);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_Tracks_RemoteId ON Tracks(RemoteId);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AccountTracks_Liked ON AccountTracks(AccountId, IsLiked);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AccountTracks_History ON AccountTracks(AccountId, LastPlayedAtUtc DESC);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AccountPlaylistTracks_Order ON AccountPlaylistTracks(AccountId, PlaylistId, TrackOrder);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_AccountPlaylistTracks_Track ON AccountPlaylistTracks(AccountId, RemoteId);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_PlaybackEvents_Account ON PlaybackEvents(AccountId, PlayedAtUtc);");
        TryExec(m_db, "CREATE INDEX IF NOT EXISTS IX_PendingLikes_Account ON PendingLikes(AccountId, UpdatedAtUtc);");

        if (!Exec(m_db, DatabaseAccountSchema::CreateEffectiveTracksViewSql))
        {
            return false;
        }

        if (migrationNeeded)
        {
            if (userVersion < 2 && !MigrateImportedAlbumCollectionsToPlaylists())
            {
                return false;
            }
            if (!Exec(m_db, "PRAGMA user_version=9;"))
            {
                return false;
            }
        }
        return true;
    }

    bool DatabaseEngine::MigrateImportedAlbumCollectionsToPlaylists()
    {
        if (!m_db)
        {
            return false;
        }

        struct ImportedAlbumCollection
        {
            int64_t Id{};
            std::wstring AlbumKey;
            std::wstring Title;
            std::wstring Artist;
            std::wstring ArtworkUrl;
            std::wstring SourceKind;
            std::wstring SourceUrl;
            std::wstring SourceLabel;
        };

        std::vector<ImportedAlbumCollection> collections;
        Statement select{ m_db,
            "SELECT Id, AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel "
            "FROM Albums WHERE COALESCE(SourceKind,'manual')<>'manual';" };
        if (!select)
        {
            return false;
        }

        while (sqlite3_step(select.value) == SQLITE_ROW)
        {
            ImportedAlbumCollection collection;
            collection.Id = sqlite3_column_int64(select.value, 0);
            collection.AlbumKey = ColumnText(select.value, 1);
            collection.Title = ColumnText(select.value, 2);
            collection.Artist = ColumnText(select.value, 3);
            collection.ArtworkUrl = ColumnText(select.value, 4);
            collection.SourceKind = ColumnText(select.value, 5);
            collection.SourceUrl = ColumnText(select.value, 6);
            collection.SourceLabel = ColumnText(select.value, 7);
            collections.push_back(std::move(collection));
        }

        if (collections.empty())
        {
            return true;
        }

        if (!Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        for (auto const& collection : collections)
        {
            auto provider = collection.SourceKind.empty() ? std::wstring{ L"provider" } : collection.SourceKind;
            auto sourceKeyPart = collection.SourceUrl.empty() ? collection.AlbumKey : collection.SourceUrl;
            auto playlistKey = L"import-playlist|" + ToLowerInvariant(provider) + L"|" + sourceKeyPart;

            TrackInfo playlist;
            playlist.Title(winrt::hstring(collection.Title.empty() ? L"Imported Playlist" : collection.Title));
            playlist.Artist(winrt::hstring(collection.Artist.empty() ? L"Imported playlist" : collection.Artist));
            ApplyArtworkUrl(playlist, winrt::hstring(collection.ArtworkUrl));
            playlist.SourceKind(L"manual");
            playlist.Provider(winrt::hstring(provider));
            playlist.SourceUrl(winrt::hstring(collection.SourceUrl));
            playlist.SourceLabel(winrt::hstring(collection.SourceLabel.empty() ? L"Imported" : collection.SourceLabel));

            auto playlistId = UpsertPlaylist(playlist, playlistKey);
            if (playlistId <= 0)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }

            Statement copyTracks{ m_db,
                "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) "
                "SELECT ?1, TrackId, TrackOrder FROM AlbumTracks WHERE AlbumId=?2 ORDER BY TrackOrder ASC;" };
            if (!copyTracks)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
            sqlite3_bind_int64(copyTracks.value, 1, playlistId);
            sqlite3_bind_int64(copyTracks.value, 2, collection.Id);
            if (sqlite3_step(copyTracks.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }

            Statement deleteLinks{ m_db, "DELETE FROM AlbumTracks WHERE AlbumId=?;" };
            if (!deleteLinks)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
            sqlite3_bind_int64(deleteLinks.value, 1, collection.Id);
            if (sqlite3_step(deleteLinks.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }

            Statement deleteAlbum{ m_db, "DELETE FROM Albums WHERE Id=?;" };
            if (!deleteAlbum)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
            sqlite3_bind_int64(deleteAlbum.value, 1, collection.Id);
            if (sqlite3_step(deleteAlbum.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
        }

        if (!Exec(m_db, "COMMIT;"))
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        return true;
    }
}
