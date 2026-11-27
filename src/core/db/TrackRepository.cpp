#include "core/db/TrackRepository.h"

#include "core/text/StrictUtf8.h"

#include "core/db/SqlTransaction.h"
#include "core/db/TrackRows.h"
#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QLatin1StringView>

#include <array>

namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr QLatin1StringView kRemote{"remote"};

// The tables a wipe empties, children before parents.
constexpr std::array<const char*, 14> kClearAllSteps{
    "UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1;",
    "DELETE FROM AccountPlaylistTracks;",
    "DELETE FROM PlaybackEvents;",
    "DELETE FROM PendingLikes;",
    "DELETE FROM AccountSyncState;",
    "DELETE FROM AccountPlaylists;",
    "DELETE FROM AccountTracks;",
    "DELETE FROM AccountProfiles;",
    "DELETE FROM FollowedArtists;",
    "DELETE FROM AlbumTracks;",
    "DELETE FROM PlaylistTracks;",
    "DELETE FROM Albums;",
    "DELETE FROM Playlists;",
    "DELETE FROM Tracks;",
};

// The upsert without its end: every upsert ends it, and a downloaded song's
// adds the condition that leaves a folder scan's row alone.
constexpr char kUpsertTrackBody[] =
    "INSERT INTO Tracks (SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsActive, RemoteId, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId, UpdatedAt) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now')) "
    "ON CONFLICT(SourceKey) DO UPDATE SET "
    "SourceKind=excluded.SourceKind, Provider=excluded.Provider, SourceUrl=excluded.SourceUrl, FilePath=excluded.FilePath, "
    "Title=excluded.Title, Artist=excluded.Artist, Album=excluded.Album, Genre=excluded.Genre, DurationSeconds=excluded.DurationSeconds, "
    "ArtworkUrl=excluded.ArtworkUrl, DateAddedSortKey=excluded.DateAddedSortKey, DateAddedText=excluded.DateAddedText, DurationText=excluded.DurationText, "
    "IsActive=excluded.IsActive, RemoteId=excluded.RemoteId, "
    "RemoteCatalogId=CASE WHEN excluded.RemoteCatalogId<>'' THEN excluded.RemoteCatalogId ELSE Tracks.RemoteCatalogId END, "
    "AlbumCatalogId=CASE WHEN excluded.AlbumCatalogId<>'' THEN excluded.AlbumCatalogId ELSE Tracks.AlbumCatalogId END, "
    "ArtistCatalogId=CASE WHEN excluded.ArtistCatalogId<>'' THEN excluded.ArtistCatalogId ELSE Tracks.ArtistCatalogId END, "
    "UpdatedAt=excluded.UpdatedAt";
constexpr char kUpsertDownloadCondition[] = " WHERE COALESCE(Tracks.Provider,'')='download' OR Tracks.IsActive=0";

constexpr char kMergePlaybackStatsSql[] =
    "UPDATE Tracks SET "
    "PlayCount=MAX(COALESCE(PlayCount,0), ?), "
    "LastPlayedOrder=MAX(COALESCE(LastPlayedOrder,0), ?), "
    "LastPlayed=CASE WHEN ? > COALESCE(LastPlayedOrder,0) THEN datetime('now') ELSE LastPlayed END "
    "WHERE SourceKey=?;";

// An address a remote row may keep; anything else is stored empty.
QString durableOrEmpty(const QString& sourceKind, const QString& url)
{
    if (sourceKind == kRemote && !net::isSafeRemoteUrl(url, net::RemoteUrlUse::Durable)) {
        return {};
    }
    return url;
}

} // namespace

TrackRepository::TrackRepository(Database& database)
    : m_connection(database.connection())
{
}

Result<void> TrackRepository::clearAllUserData()
{
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    for (const char* step : kClearAllSteps) {
        if (const Result<void> done = m_connection.exec(step); !done) {
            return tx::rollbackWith(m_connection, done.failure());
        }
    }
    tx::tryExec(m_connection, "DELETE FROM sqlite_sequence WHERE name IN ('Tracks','Albums','Playlists');");
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }

    if (const Result<void> checkpoint = m_connection.exec("PRAGMA wal_checkpoint(TRUNCATE);"); !checkpoint) {
        return checkpoint;
    }
    return m_connection.exec("VACUUM;");
}

void TrackRepository::beginLocalScan()
{
    // Deliberately empty: see the declaration.
}

Result<void> TrackRepository::completeLocalScan(const QStringList& activeSourceKeys)
{
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    if (const Result<void> cleared = m_connection.exec(
            "UPDATE Tracks SET IsActive=0 WHERE SourceKind='local' AND COALESCE(Provider,'')<>'download';");
        !cleared) {
        return tx::rollbackWith(m_connection, cleared.failure());
    }

    auto statement = m_connection.prepare(
        "UPDATE Tracks SET IsActive=1 WHERE SourceKind='local' AND COALESCE(Provider,'')<>'download' AND SourceKey=?;");
    if (!statement) {
        return tx::rollbackWith(m_connection, statement.failure());
    }
    SqliteStatement& activate = statement.value();
    for (const QString& key : activeSourceKeys) {
        activate.reset();
        activate.clearBindings();
        activate.bindText(1, key);
        if (const Result<void> ran = activate.run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }

    // A failed commit is rolled back like every other one here (earlier
    // versions left the transaction open, and every later write then failed
    // or was lost at close).
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

Result<void> TrackRepository::syncDownloadTracks(const QList<Track>& tracks)
{
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    // Every downloaded song's row goes inactive; each song still downloaded
    // comes back active with its upsert.
    if (const Result<void> cleared =
            m_connection.exec("UPDATE Tracks SET IsActive=0 WHERE SourceKind='local' AND Provider='download';");
        !cleared) {
        return tx::rollbackWith(m_connection, cleared.failure());
    }
    const QByteArray sql = QByteArray(kUpsertTrackBody) + kUpsertDownloadCondition + ';';
    for (const Track& track : tracks) {
        const Result<qint64> written =
            upsertTrack(track, QStringLiteral("local"), QString(DownloadProvider), catalogSourceKey(track), true, sql);
        // A song without a usable key is left out; a refused write undoes all.
        if (!written && written.failure().kind != FailureKind::NotFound) {
            return tx::rollbackWith(m_connection, written.failure());
        }
    }
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

QHash<QString, double> TrackRepository::downloadTrackDurations()
{
    QHash<QString, double> durations;
    auto statement = m_connection.prepare(
        "SELECT SourceKey, DurationSeconds FROM Tracks WHERE SourceKind='local' AND Provider='download' "
        "AND COALESCE(DurationSeconds,0)>0;");
    if (!statement) {
        return durations;
    }
    while (statement.value().step().valueOr(false)) {
        durations.insert(statement.value().columnText(0), statement.value().columnDouble(1));
    }
    return durations;
}

Result<qint64> TrackRepository::upsertLocalTrack(const Track& track, const QString& sourceKey)
{
    return upsertTrack(track, QStringLiteral("local"), QString(), sourceKey, true,
                       QByteArray(kUpsertTrackBody) + ';');
}

Result<qint64> TrackRepository::upsertRemoteTrack(const Track& track, const QString& sourceKey)
{
    const QString provider = track.origin.isEmpty() ? QStringLiteral("remote") : track.origin;
    return upsertTrack(track, QStringLiteral("remote"), provider, sourceKey, true,
                       QByteArray(kUpsertTrackBody) + ';');
}

Result<qint64> TrackRepository::upsertTrack(const Track& track,
                                             const QString& sourceKind,
                                             const QString& provider,
                                             const QString& sourceKey,
                                             bool active,
                                             const QByteArray& sql)
{
    if (sourceKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty source key"));
    }
    // A key that is not valid text (a file name with a lone surrogate) would
    // be bound as NULL, which never matches the row it made: every scan would
    // add another. Such a song is left out instead.
    if (!text::encodeUtf8Strict(sourceKey)) {
        return tx::notFound(QStringLiteral("source key is not valid text"));
    }

    auto prepared = m_connection.prepare(sql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();

    QString sourceUrl = track.sourceUrl;
    if (sourceUrl.isEmpty() && sourceKind == kRemote) {
        sourceUrl = track.filePath;
    }
    sourceUrl = durableOrEmpty(sourceKind, sourceUrl);

    statement.bindText(1, sourceKey);
    statement.bindText(2, sourceKind);
    statement.bindText(3, provider);
    statement.bindText(4, sourceUrl);
    statement.bindText(5, track_rows::storedFilePathFor(track, sourceKind));
    statement.bindText(6, track.title);
    statement.bindText(7, track.artist);
    statement.bindText(8, track.album);
    statement.bindText(9, track.genre);
    statement.bindDouble(10, track.durationSeconds);
    statement.bindText(11, durableOrEmpty(sourceKind, track.artworkUrl));
    statement.bindDouble(12, track.dateAddedSortKey);
    statement.bindText(13, track.dateAdded);
    statement.bindText(14, track.durationText);
    statement.bindInt64(15, active ? 1 : 0);
    statement.bindText(16, track.remoteId);
    statement.bindText(17, track.catalogId);
    statement.bindText(18, track.albumCatalogId);
    statement.bindText(19, track.artistCatalogId);
    if (const Result<void> ran = statement.run(); !ran) {
        return ran.failure();
    }

    auto lookup = m_connection.prepare("SELECT Id FROM Tracks WHERE SourceKey=?;");
    if (!lookup) {
        return lookup.failure();
    }
    lookup.value().bindText(1, sourceKey);
    if (lookup.value().step().valueOr(false)) {
        return lookup.value().columnInt64(0);
    }
    return m_connection.lastInsertRowId();
}

Result<void> TrackRepository::recordPlayback(const QString& sourceKey, quint64 playOrder)
{
    if (sourceKey.isEmpty()) {
        return Result<void>::success();
    }
    auto statement = m_connection.prepare(
        "UPDATE Tracks SET PlayCount=COALESCE(PlayCount,0)+1, LastPlayedOrder=?, LastPlayed=datetime('now'), LastPlayedExact=1 WHERE SourceKey=?;");
    if (!statement) {
        return statement.failure();
    }
    // Stored as SQLite's signed integer, as before.
    statement.value().bindInt64(1, static_cast<qint64>(playOrder));
    statement.value().bindText(2, sourceKey);
    return statement.value().run();
}

Result<void> TrackRepository::recordPlaybackPosition(const QString& sourceKey, double positionSeconds)
{
    if (sourceKey.isEmpty()) {
        return Result<void>::success();
    }
    auto statement = m_connection.prepare("UPDATE Tracks SET LastPositionSeconds=? WHERE SourceKey=?;");
    if (!statement) {
        return statement.failure();
    }
    statement.value().bindDouble(1, positionSeconds > 0.0 ? positionSeconds : 0.0);
    statement.value().bindText(2, sourceKey);
    return statement.value().run();
}

Result<void> TrackRepository::mergePlaybackStats(const QString& sourceKey, quint32 playCount, quint64 lastPlayedOrder)
{
    if (sourceKey.isEmpty() || (playCount == 0 && lastPlayedOrder == 0)) {
        return Result<void>::success();
    }
    auto statement = m_connection.prepare(kMergePlaybackStatsSql);
    if (!statement) {
        return statement.failure();
    }
    // The count is bound as a 32-bit int, as before.
    statement.value().bindInt64(1, static_cast<int>(playCount));
    statement.value().bindInt64(2, static_cast<qint64>(lastPlayedOrder));
    statement.value().bindInt64(3, static_cast<qint64>(lastPlayedOrder));
    statement.value().bindText(4, sourceKey);
    return statement.value().run();
}

quint64 TrackRepository::maxLastPlayedOrder()
{
    auto statement = m_connection.prepare("SELECT COALESCE(MAX(LastPlayedOrder),0) FROM Tracks;");
    if (!statement || !statement.value().step().valueOr(false)) {
        return 0;
    }
    const qint64 value = statement.value().columnInt64(0);
    return value > 0 ? static_cast<quint64>(value) : 0;
}

Result<void> TrackRepository::setLiked(const QString& sourceKey, bool liked)
{
    if (sourceKey.isEmpty()) {
        return Result<void>::success();
    }
    auto statement = m_connection.prepare("UPDATE Tracks SET IsLiked=? WHERE SourceKey=?;");
    if (!statement) {
        return statement.failure();
    }
    statement.value().bindInt64(1, liked ? 1 : 0);
    statement.value().bindText(2, sourceKey);
    return statement.value().run();
}

bool TrackRepository::isLiked(const QString& sourceKey)
{
    if (sourceKey.isEmpty()) {
        return false;
    }
    auto statement = m_connection.prepare("SELECT IsLiked FROM Tracks WHERE SourceKey=?;");
    if (!statement) {
        return false;
    }
    statement.value().bindText(1, sourceKey);
    return statement.value().step().valueOr(false) && static_cast<int>(statement.value().columnInt64(0)) != 0;
}

} // namespace lmp::core::db
