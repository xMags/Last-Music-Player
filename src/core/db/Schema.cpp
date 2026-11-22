#include "core/db/Schema.h"

#include "core/db/AccountSchema.h"
#include "core/db/CollectionSql.h"
#include "core/db/SqliteConnection.h"
#include "core/db/TrackSchema.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QList>

#include <utility>

namespace lmp::core::db {

namespace {

constexpr char kCreateTracksTableSql[] =
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
    // Where playback stopped, so a track can be picked up rather than
    // restarted. Zero means "from the beginning".
    "LastPositionSeconds REAL DEFAULT 0,"
    "RemoteId TEXT,"
    "RemoteCatalogId TEXT,"
    "AlbumCatalogId TEXT,"
    "ArtistCatalogId TEXT);";

constexpr char kCreateAlbumsSql[] =
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
    "UpdatedAt INTEGER DEFAULT 0);";

constexpr char kCreateAlbumTracksSql[] =
    "CREATE TABLE IF NOT EXISTS AlbumTracks ("
    "AlbumId INTEGER NOT NULL,"
    "TrackId INTEGER NOT NULL,"
    "TrackOrder INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (AlbumId, TrackId),"
    "FOREIGN KEY (AlbumId) REFERENCES Albums(Id) ON DELETE CASCADE,"
    "FOREIGN KEY (TrackId) REFERENCES Tracks(Id) ON DELETE CASCADE);";

constexpr char kCreatePlaylistsSql[] =
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
    "UpdatedAt INTEGER DEFAULT 0);";

constexpr char kCreatePlaylistTracksSql[] =
    "CREATE TABLE IF NOT EXISTS PlaylistTracks ("
    "PlaylistId INTEGER NOT NULL,"
    "TrackId INTEGER NOT NULL,"
    "TrackOrder INTEGER NOT NULL DEFAULT 0,"
    "PRIMARY KEY (PlaylistId, TrackId),"
    "FOREIGN KEY (PlaylistId) REFERENCES Playlists(Id) ON DELETE CASCADE,"
    "FOREIGN KEY (TrackId) REFERENCES Tracks(Id) ON DELETE CASCADE);";

// Version 1 to 2: new columns, a source key for every row, duplicates removed.
constexpr const char* kVersion2Steps[] = {
    "ALTER TABLE Tracks ADD COLUMN SourceKey TEXT;",
    "ALTER TABLE Tracks ADD COLUMN SourceKind TEXT DEFAULT 'local';",
    "ALTER TABLE Tracks ADD COLUMN Provider TEXT;",
    "ALTER TABLE Tracks ADD COLUMN SourceUrl TEXT;",
    "ALTER TABLE Tracks ADD COLUMN Album TEXT;",
    "ALTER TABLE Tracks ADD COLUMN Genre TEXT;",
    "ALTER TABLE Tracks ADD COLUMN DurationSeconds REAL DEFAULT 0;",
    "ALTER TABLE Tracks ADD COLUMN ArtworkUrl TEXT;",
    "ALTER TABLE Tracks ADD COLUMN DateAddedSortKey REAL DEFAULT 0;",
    "ALTER TABLE Tracks ADD COLUMN DateAddedText TEXT;",
    "ALTER TABLE Tracks ADD COLUMN DurationText TEXT;",
    "ALTER TABLE Tracks ADD COLUMN PlayCount INTEGER DEFAULT 0;",
    "ALTER TABLE Tracks ADD COLUMN LastPlayedOrder INTEGER DEFAULT 0;",
    "ALTER TABLE Tracks ADD COLUMN LastPlayed DATETIME;",
    "ALTER TABLE Tracks ADD COLUMN IsLiked INTEGER DEFAULT 0;",
    "ALTER TABLE Tracks ADD COLUMN IsActive INTEGER DEFAULT 1;",
    "ALTER TABLE Tracks ADD COLUMN UpdatedAt INTEGER DEFAULT 0;",
    "ALTER TABLE Playlists ADD COLUMN Description TEXT;",
    "ALTER TABLE Playlists ADD COLUMN ArtworkUrl TEXT;",
    "ALTER TABLE Playlists ADD COLUMN SourceKind TEXT DEFAULT 'manual';",
    "ALTER TABLE Playlists ADD COLUMN Provider TEXT;",
    "ALTER TABLE Playlists ADD COLUMN SourceUrl TEXT;",
    "ALTER TABLE Playlists ADD COLUMN SourceLabel TEXT;",
    "ALTER TABLE Playlists ADD COLUMN CreatedAt INTEGER DEFAULT 0;",
    "ALTER TABLE Playlists ADD COLUMN UpdatedAt INTEGER DEFAULT 0;",
    "UPDATE Tracks SET SourceKind='local' WHERE SourceKind IS NULL OR SourceKind='';",
    "UPDATE Tracks SET IsActive=1 WHERE IsActive IS NULL;",
    "UPDATE Tracks SET SourceKey='local|' || lower(FilePath) WHERE (SourceKey IS NULL OR SourceKey='') AND FilePath IS NOT NULL AND FilePath <> '';",
    "UPDATE Tracks SET SourceKey='remote|' || lower(COALESCE(NULLIF(Provider,''),'provider')) || '|' || SourceUrl WHERE SourceKind='remote' AND SourceUrl IS NOT NULL AND SourceUrl <> '';",
    "UPDATE Tracks SET SourceKey='legacy|' || Id WHERE SourceKey IS NULL OR SourceKey='';",
    "DELETE FROM Tracks WHERE Id NOT IN (SELECT MIN(Id) FROM Tracks GROUP BY SourceKey);",
};

// Merges remote rows that point at the same source into the oldest one,
// keeping the highest like, play count and play times of the group.
constexpr char kNormalizeRemoteSourceKeysSql[] =
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyMap;"
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyKeepers;"
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyAgg;"
    "CREATE TEMP TABLE RemoteSourceKeyMap AS "
    "SELECT Id, 'remote|' || lower(COALESCE(NULLIF(Provider,''),'provider')) || '|' || SourceUrl AS TargetKey "
    "FROM Tracks "
    "WHERE SourceKind='remote' AND SourceUrl IS NOT NULL AND SourceUrl<>'';"
    "CREATE UNIQUE INDEX IX_RemoteSourceKeyMap_Id ON RemoteSourceKeyMap(Id);"
    "CREATE INDEX IX_RemoteSourceKeyMap_Key ON RemoteSourceKeyMap(TargetKey);"
    "CREATE TEMP TABLE RemoteSourceKeyKeepers AS "
    "SELECT TargetKey, MIN(Id) AS KeepId "
    "FROM RemoteSourceKeyMap "
    "GROUP BY TargetKey;"
    "CREATE UNIQUE INDEX IX_RemoteSourceKeyKeepers_Key ON RemoteSourceKeyKeepers(TargetKey);"
    "CREATE UNIQUE INDEX IX_RemoteSourceKeyKeepers_KeepId ON RemoteSourceKeyKeepers(KeepId);"
    "CREATE TEMP TABLE RemoteSourceKeyAgg AS "
    "SELECT m.TargetKey AS TargetKey,"
    "MAX(COALESCE(t.IsLiked,0)) AS IsLiked,"
    "MAX(COALESCE(t.PlayCount,0)) AS PlayCount,"
    "MAX(COALESCE(t.LastPlayedOrder,0)) AS LastPlayedOrder,"
    "MAX(COALESCE(t.UpdatedAt,0)) AS UpdatedAt,"
    "MAX(t.LastPlayed) AS LastPlayed "
    "FROM Tracks t JOIN RemoteSourceKeyMap m ON m.Id=t.Id "
    "GROUP BY m.TargetKey;"
    "CREATE UNIQUE INDEX IX_RemoteSourceKeyAgg_Key ON RemoteSourceKeyAgg(TargetKey);"
    "UPDATE Tracks SET "
    "(IsLiked, PlayCount, LastPlayedOrder, UpdatedAt, LastPlayed)="
    "(SELECT a.IsLiked, a.PlayCount, a.LastPlayedOrder, a.UpdatedAt, a.LastPlayed "
    "FROM RemoteSourceKeyKeepers k JOIN RemoteSourceKeyAgg a ON a.TargetKey=k.TargetKey "
    "WHERE k.KeepId=Tracks.Id) "
    "WHERE Id IN (SELECT KeepId FROM RemoteSourceKeyKeepers);"
    "DELETE FROM Tracks "
    "WHERE Id IN (SELECT Id FROM RemoteSourceKeyMap WHERE Id NOT IN (SELECT KeepId FROM RemoteSourceKeyKeepers));"
    "UPDATE Tracks SET SourceKey=(SELECT TargetKey FROM RemoteSourceKeyMap WHERE Id=Tracks.Id) "
    "WHERE Id IN (SELECT KeepId FROM RemoteSourceKeyKeepers) "
    "AND COALESCE(SourceKey,'')<>(SELECT TargetKey FROM RemoteSourceKeyMap WHERE Id=Tracks.Id);"
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyMap;"
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyKeepers;"
    "DROP TABLE IF EXISTS temp.RemoteSourceKeyAgg;";

// Very old databases declared FilePath UNIQUE; the table is rebuilt without it.
constexpr char kRebuildTracksSql[] =
    "DROP TABLE IF EXISTS Tracks_rebuild;"
    "CREATE TABLE Tracks_rebuild ("
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
    "RemoteId TEXT,"
    "LastPositionSeconds REAL DEFAULT 0,"
    "RemoteCatalogId TEXT,"
    "AlbumCatalogId TEXT,"
    "ArtistCatalogId TEXT);"
    "INSERT INTO Tracks_rebuild (Id, SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, PlayCount, LastPlayedOrder, IsLiked, IsActive, UpdatedAt, LastPlayed, LastPlayedExact, RemoteId, LastPositionSeconds, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId) "
    "SELECT Id, SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, PlayCount, LastPlayedOrder, IsLiked, IsActive, UpdatedAt, LastPlayed, COALESCE(LastPlayedExact,0), RemoteId, COALESCE(LastPositionSeconds,0), RemoteCatalogId, AlbumCatalogId, ArtistCatalogId FROM Tracks;"
    "DROP TABLE Tracks;"
    "ALTER TABLE Tracks_rebuild RENAME TO Tracks;";

// Tried on every start; the first must succeed.
constexpr char kCreateSourceKeyIndexSql[] = "CREATE UNIQUE INDEX IF NOT EXISTS IX_Tracks_SourceKey ON Tracks(SourceKey);";
constexpr const char* kIndexSteps[] = {
    "CREATE INDEX IF NOT EXISTS IX_Tracks_SourceKind_Active ON Tracks(SourceKind, IsActive);",
    "CREATE INDEX IF NOT EXISTS IX_Tracks_Album ON Tracks(Album COLLATE NOCASE);",
    "CREATE INDEX IF NOT EXISTS IX_Tracks_Artist ON Tracks(Artist COLLATE NOCASE);",
    "CREATE INDEX IF NOT EXISTS IX_Tracks_Genre ON Tracks(Genre COLLATE NOCASE);",
    "CREATE INDEX IF NOT EXISTS IX_Tracks_DateAdded ON Tracks(DateAddedSortKey DESC);",
    "CREATE INDEX IF NOT EXISTS IX_AlbumTracks_TrackId ON AlbumTracks(TrackId);",
    "CREATE INDEX IF NOT EXISTS IX_AlbumTracks_Order ON AlbumTracks(AlbumId, TrackOrder);",
    "CREATE INDEX IF NOT EXISTS IX_PlaylistTracks_TrackId ON PlaylistTracks(TrackId);",
    "CREATE INDEX IF NOT EXISTS IX_PlaylistTracks_Order ON PlaylistTracks(PlaylistId, TrackOrder);",
    "CREATE INDEX IF NOT EXISTS IX_Tracks_RemoteId ON Tracks(RemoteId);",
    "CREATE INDEX IF NOT EXISTS IX_AccountTracks_Liked ON AccountTracks(AccountId, IsLiked);",
    "CREATE INDEX IF NOT EXISTS IX_AccountTracks_History ON AccountTracks(AccountId, LastPlayedAtUtc DESC);",
    "CREATE INDEX IF NOT EXISTS IX_AccountPlaylistTracks_Order ON AccountPlaylistTracks(AccountId, PlaylistId, TrackOrder);",
    "CREATE INDEX IF NOT EXISTS IX_AccountPlaylistTracks_Track ON AccountPlaylistTracks(AccountId, RemoteId);",
    "CREATE INDEX IF NOT EXISTS IX_PlaybackEvents_Account ON PlaybackEvents(AccountId, PlayedAtUtc);",
    "CREATE INDEX IF NOT EXISTS IX_PendingLikes_Account ON PendingLikes(AccountId, UpdatedAtUtc);",
    "CREATE INDEX IF NOT EXISTS IX_PlaybackEvents_Retry ON PlaybackEvents(AccountId, IsBlocked, NextAttemptAtUnix);",
    "CREATE INDEX IF NOT EXISTS IX_PendingLikes_Retry ON PendingLikes(AccountId, IsBlocked, NextAttemptAtUnix);",
    "CREATE INDEX IF NOT EXISTS IX_AccountPreferences_Retry ON AccountPreferences(AccountId, Pending, IsBlocked, NextAttemptAtUnix);",
};

void tryExec(SqliteConnection& connection, const char* sql)
{
    // A step that does not apply to this database's shape fails harmlessly.
    static_cast<void>(connection.exec(sql));
}

} // namespace

int Schema::userVersion(SqliteConnection& connection)
{
    Result<SqliteStatement> statement = connection.prepare("PRAGMA user_version;");
    if (!statement || !statement.value().step().valueOr(false)) {
        return 0;
    }
    return static_cast<int>(statement.value().columnInt64(0));
}

Result<void> Schema::ensure(SqliteConnection& connection)
{
    if (const Result<void> created = createTables(connection); !created) {
        return created;
    }

    // The heavy one-time migration runs only for a database older than the
    // current version; everything after it is idempotent and runs each time.
    const int version = userVersion(connection);
    const bool migrationNeeded = version < CurrentVersion;
    if (migrationNeeded) {
        migrateColumns(connection, version);
        if (version < 2) {
            if (const Result<void> keyed = migrateToSourceKeys(connection); !keyed) {
                return keyed;
            }
        }
    }

    // Account playback belongs in AccountTracks; rows written by builds from
    // before that split are removed so another account's plays never show.
    if (const Result<void> removed = connection.exec(account_schema::RemoveLegacyGenericAccountTracksSql); !removed) {
        return removed;
    }
    if (const Result<void> cleaned = connection.exec(account_schema::RemoveTransientRemoteUrlsSql); !cleaned) {
        return cleaned;
    }
    if (const Result<void> indexed = createIndexesAndView(connection); !indexed) {
        return indexed;
    }

    if (migrationNeeded) {
        if (version < 2) {
            if (const Result<void> moved = migrateImportedAlbumCollectionsToPlaylists(connection); !moved) {
                return moved;
            }
        }
        if (const Result<void> stamped = connection.exec("PRAGMA user_version=14;"); !stamped) {
            return stamped;
        }
    }
    return Result<void>::success();
}

Result<void> Schema::createTables(SqliteConnection& connection)
{
    for (const char* sql : {kCreateTracksTableSql, kCreateAlbumsSql, kCreateAlbumTracksSql, kCreatePlaylistsSql,
                            kCreatePlaylistTracksSql, account_schema::CreateTablesSql,
                            account_schema::CreateFollowedArtistsSql}) {
        if (const Result<void> done = connection.exec(sql); !done) {
            return done;
        }
    }
    return Result<void>::success();
}

void Schema::migrateColumns(SqliteConnection& connection, int fromVersion)
{
    // In the same order as earlier versions ran them.
    if (fromVersion < 13) {
        tryExec(connection, track_schema::AddRemoteCatalogIdSql);
        tryExec(connection, track_schema::AddAlbumCatalogIdSql);
        tryExec(connection, track_schema::AddArtistCatalogIdSql);
    }
    if (fromVersion < 12) {
        tryExec(connection, account_schema::AddAccountTrackProviderSql);
        tryExec(connection, account_schema::AddAccountTrackCatalogIdSql);
        tryExec(connection, account_schema::AddAccountTrackAlbumCatalogIdSql);
        tryExec(connection, account_schema::AddAccountTrackArtistCatalogIdSql);
    }
    if (fromVersion < 11) {
        tryExec(connection, account_schema::AddPlaybackNextAttemptSql);
        tryExec(connection, account_schema::AddPlaybackFailureCategorySql);
        tryExec(connection, account_schema::AddPlaybackBlockedSql);
        tryExec(connection, account_schema::AddLikeNextAttemptSql);
        tryExec(connection, account_schema::AddLikeFailureCategorySql);
        tryExec(connection, account_schema::AddLikeBlockedSql);
    }
    if (fromVersion < 10) {
        tryExec(connection, "ALTER TABLE Tracks ADD COLUMN LastPositionSeconds REAL DEFAULT 0;");
    }
    if (fromVersion < 9) {
        tryExec(connection, "ALTER TABLE Tracks ADD COLUMN LastPlayedExact INTEGER DEFAULT 0;");
        // Play times written before version 9 came from real playback.
        tryExec(connection, "UPDATE Tracks SET LastPlayedExact=1 WHERE COALESCE(LastPlayed,'')<>'';");
    }
    if (fromVersion < 8) {
        tryExec(connection, "ALTER TABLE ActiveAccountContext ADD COLUMN RemoteMode TEXT NOT NULL DEFAULT 'LocalOnly';");
    }
    if (fromVersion < 7) {
        tryExec(connection, "ALTER TABLE Tracks ADD COLUMN RemoteId TEXT;");
    }
    if (fromVersion < 6) {
        // Imported collection labels become neutral.
        tryExec(connection, "UPDATE Albums SET Artist='Music API' WHERE SourceKind<>'manual' AND COALESCE(SourceUrl,'')<>'' AND Artist=SourceLabel;");
        tryExec(connection, "UPDATE Albums SET SourceLabel='Music API' WHERE SourceKind<>'manual' AND COALESCE(SourceUrl,'')<>'' AND COALESCE(SourceLabel,'')<>'';");
        tryExec(connection, "UPDATE Playlists SET Description='Music API' WHERE COALESCE(Provider,'')<>'' AND COALESCE(SourceUrl,'')<>'' AND Description=SourceLabel || ' import';");
        tryExec(connection, "UPDATE Playlists SET SourceLabel='Music API' WHERE COALESCE(Provider,'')<>'' AND COALESCE(SourceUrl,'')<>'' AND COALESCE(SourceLabel,'')<>'';");
    }
    if (fromVersion < 4) {
        // The unused ReplayGainDb column goes; older databases never had it.
        tryExec(connection, "ALTER TABLE Tracks DROP COLUMN ReplayGainDb;");
    }
    if (fromVersion < 5) {
        tryExec(connection, "UPDATE Tracks SET DateAddedText='Music API' WHERE SourceKind='remote';");
    }
    if (fromVersion < 2) {
        for (const char* sql : kVersion2Steps) {
            tryExec(connection, sql);
        }
    }
}

Result<void> Schema::migrateToSourceKeys(SqliteConnection& connection)
{
    if (const Result<void> normalized = runInTransaction(connection, kNormalizeRemoteSourceKeysSql); !normalized) {
        return normalized;
    }
    if (tracksHasLegacyFilePathUnique(connection)) {
        return runInTransaction(connection, kRebuildTracksSql);
    }
    return Result<void>::success();
}

Result<void> Schema::createIndexesAndView(SqliteConnection& connection)
{
    if (const Result<void> unique = connection.exec(kCreateSourceKeyIndexSql); !unique) {
        return unique;
    }
    for (const char* sql : kIndexSteps) {
        tryExec(connection, sql);
    }
    return connection.exec(account_schema::CreateEffectiveTracksViewSql);
}

bool Schema::tracksHasLegacyFilePathUnique(SqliteConnection& connection)
{
    Result<SqliteStatement> statement =
        connection.prepare("SELECT sql FROM sqlite_master WHERE type='table' AND name='Tracks';");
    if (!statement || !statement.value().step().valueOr(false)) {
        return false;
    }
    return statement.value().columnText(0).contains(QLatin1StringView("FilePath TEXT UNIQUE"));
}

Result<void> Schema::runInTransaction(SqliteConnection& connection, const char* sql)
{
    if (const Result<void> begun = connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    if (const Result<void> done = connection.exec(sql); !done) {
        tryExec(connection, "ROLLBACK;");
        return done;
    }
    if (const Result<void> committed = connection.exec("COMMIT;"); !committed) {
        tryExec(connection, "ROLLBACK;");
        return committed;
    }
    return Result<void>::success();
}

Result<void> Schema::migrateImportedAlbumCollectionsToPlaylists(SqliteConnection& connection)
{
    struct ImportedAlbumCollection {
        qint64 id = 0;
        QString albumKey;
        QString title;
        QString artist;
        QString artworkUrl;
        QString sourceKind;
        QString sourceUrl;
        QString sourceLabel;
    };

    QList<ImportedAlbumCollection> collections;
    {
        Result<SqliteStatement> select = connection.prepare(
            "SELECT Id, AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel "
            "FROM Albums WHERE COALESCE(SourceKind,'manual')<>'manual';");
        if (!select) {
            return select.failure();
        }
        SqliteStatement& rows = select.value();
        while (rows.step().valueOr(false)) {
            collections.append({rows.columnInt64(0), rows.columnText(1), rows.columnText(2), rows.columnText(3),
                                rows.columnText(4), rows.columnText(5), rows.columnText(6), rows.columnText(7)});
        }
    }
    if (collections.isEmpty()) {
        return Result<void>::success();
    }

    if (const Result<void> begun = connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    const auto rollback = [&connection](const Failure& failure) -> Result<void> {
        tryExec(connection, "ROLLBACK;");
        return failure;
    };

    for (const ImportedAlbumCollection& collection : std::as_const(collections)) {
        const QString provider = collection.sourceKind.isEmpty() ? QStringLiteral("provider") : collection.sourceKind;
        const QString keyPart = collection.sourceUrl.isEmpty() ? collection.albumKey : collection.sourceUrl;
        const QString playlistKey = QStringLiteral("import-playlist|") + provider.toLower() + QLatin1Char('|') + keyPart;

        QString artworkUrl = collection.artworkUrl;
        if (provider != QLatin1StringView("manual")
            && !net::isSafeRemoteUrl(artworkUrl, net::RemoteUrlUse::Durable)) {
            artworkUrl.clear();
        }

        Result<SqliteStatement> upsert = connection.prepare(collection_sql::UpsertPlaylistSql);
        if (!upsert) {
            return rollback(upsert.failure());
        }
        SqliteStatement& insert = upsert.value();
        insert.bindText(1, playlistKey);
        insert.bindText(2, collection.title.isEmpty() ? QStringLiteral("Imported Playlist") : collection.title);
        insert.bindText(3, collection.artist.isEmpty() ? QStringLiteral("Imported playlist") : collection.artist);
        insert.bindText(4, artworkUrl);
        insert.bindText(5, QStringLiteral("manual"));
        insert.bindText(6, provider);
        insert.bindText(7, collection.sourceUrl);
        insert.bindText(8, collection.sourceLabel.isEmpty() ? QStringLiteral("Imported") : collection.sourceLabel);
        if (const Result<void> inserted = insert.run(); !inserted) {
            return rollback(inserted.failure());
        }

        qint64 playlistId = connection.lastInsertRowId();
        if (Result<SqliteStatement> lookup = connection.prepare(collection_sql::PlaylistIdByKeySql); lookup) {
            lookup.value().bindText(1, playlistKey);
            if (lookup.value().step().valueOr(false)) {
                playlistId = lookup.value().columnInt64(0);
            }
        } else {
            return rollback(lookup.failure());
        }
        if (playlistId <= 0) {
            return rollback(Failure::of(FailureKind::Storage, QStringLiteral("imported playlist has no id")));
        }

        Result<SqliteStatement> copy = connection.prepare(
            "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) "
            "SELECT ?1, TrackId, TrackOrder FROM AlbumTracks WHERE AlbumId=?2 ORDER BY TrackOrder ASC;");
        if (!copy) {
            return rollback(copy.failure());
        }
        copy.value().bindInt64(1, playlistId);
        copy.value().bindInt64(2, collection.id);
        if (const Result<void> copied = copy.value().run(); !copied) {
            return rollback(copied.failure());
        }

        for (const char* sql : {"DELETE FROM AlbumTracks WHERE AlbumId=?;", "DELETE FROM Albums WHERE Id=?;"}) {
            Result<SqliteStatement> remove = connection.prepare(sql);
            if (!remove) {
                return rollback(remove.failure());
            }
            remove.value().bindInt64(1, collection.id);
            if (const Result<void> removed = remove.value().run(); !removed) {
                return rollback(removed.failure());
            }
        }
    }

    if (const Result<void> committed = connection.exec("COMMIT;"); !committed) {
        return rollback(committed.failure());
    }
    return Result<void>::success();
}

} // namespace lmp::core::db
