#include "core/db/CollectionRepository.h"

#include "core/db/CollectionSql.h"
#include "core/db/PlaylistBulkInsert.h"
#include "core/db/SqlTransaction.h"

#include <QSet>

// Editing the songs of a playlist: reorder, add, remove, move, and renaming
// and deleting playlists.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr char kPlaylistIdByNumberedKeySql[] = "SELECT Id FROM Playlists WHERE PlaylistKey=?1;";
constexpr char kRemoveMemberSql[] = "DELETE FROM PlaylistTracks WHERE PlaylistId=?1 AND TrackId=?2;";

constexpr char kAppendMovedMemberSql[] =
    "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) "
    "SELECT ?1, TrackId, ?2 FROM PlaylistTracks "
    "WHERE PlaylistId=?3 AND TrackId=?4;";

} // namespace

Result<void> CollectionRepository::reorderPlaylistTracks(const QString& playlistKey, const QList<qint64>& trackIds)
{
    if (playlistKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty playlist key"));
    }
    const qint64 playlistId = lookupId(collection_sql::PlaylistIdByKeySql, playlistKey);
    if (playlistId == 0) {
        return tx::notFound(QStringLiteral("no playlist with that key"));
    }
    // The read-back below decides the outcome, so a failed replace shows
    // up there rather than here.
    static_cast<void>(replacePlaylistTracks(playlistId, trackIds));

    auto verify = m_connection.prepare("SELECT TrackId FROM PlaylistTracks WHERE PlaylistId=? ORDER BY TrackOrder;");
    if (!verify) {
        return verify.failure();
    }
    verify.value().bindInt64(1, playlistId);
    qsizetype index = 0;
    while (verify.value().step().valueOr(false)) {
        if (index >= trackIds.size() || verify.value().columnInt64(0) != trackIds.at(index)) {
            return tx::notFound(QStringLiteral("stored order differs"));
        }
        ++index;
    }
    if (index != trackIds.size()) {
        return tx::notFound(QStringLiteral("stored order differs"));
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::addTrackToPlaylist(const QString& playlistKey, qint64 trackId)
{
    return addTracksToPlaylist(playlistKey, QList<qint64>{trackId});
}

Result<void> CollectionRepository::addTracksToPlaylist(const QString& playlistKey, const QList<qint64>& trackIds)
{
    return addTracksToPlaylistTransactional(m_connection, playlistKey, trackIds);
}

Result<void> CollectionRepository::removeTrackFromPlaylist(const QString& playlistKey, qint64 trackId)
{
    if (playlistKey.isEmpty() || trackId <= 0) {
        return tx::notFound(QStringLiteral("nothing to remove"));
    }
    auto remove = m_connection.prepare(
        "DELETE FROM PlaylistTracks WHERE TrackId=?1 AND PlaylistId=(SELECT Id FROM Playlists WHERE PlaylistKey=?2);");
    if (!remove) {
        return remove.failure();
    }
    remove.value().bindInt64(1, trackId);
    remove.value().bindText(2, playlistKey);
    if (const Result<void> ran = remove.value().run(); !ran) {
        return ran;
    }

    // Touching the timestamp is best effort, as before.
    if (auto touch = m_connection.prepare("UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE PlaylistKey=?;");
        touch) {
        touch.value().bindText(1, playlistKey);
        static_cast<void>(touch.value().step());
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::removeTracksFromPlaylist(const QString& playlistKey, const QList<qint64>& trackIds)
{
    if (playlistKey.isEmpty() || trackIds.isEmpty()) {
        return tx::notFound(QStringLiteral("nothing to remove"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    auto lookup = m_connection.prepare(kPlaylistIdByNumberedKeySql);
    if (!lookup) {
        return tx::rollbackWith(m_connection, lookup.failure());
    }
    lookup.value().bindText(1, playlistKey);
    if (!lookup.value().step().valueOr(false)) {
        return tx::rollbackWith(m_connection, tx::notFound(QStringLiteral("no playlist with that key")));
    }
    const qint64 playlistId = lookup.value().columnInt64(0);

    auto remove = m_connection.prepare(kRemoveMemberSql);
    if (!remove) {
        return tx::rollbackWith(m_connection, remove.failure());
    }
    QSet<qint64> uniqueIds;
    int removed = 0;
    for (const qint64 trackId : trackIds) {
        if (trackId <= 0 || uniqueIds.contains(trackId)) {
            continue;
        }
        uniqueIds.insert(trackId);
        remove.value().reset();
        remove.value().clearBindings();
        remove.value().bindInt64(1, playlistId);
        remove.value().bindInt64(2, trackId);
        if (const Result<void> ran = remove.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
        removed += m_connection.changes();
    }
    if (removed == 0) {
        return tx::rollbackWith(m_connection, tx::notFound(QStringLiteral("no listed song was in the playlist")));
    }

    auto touch = m_connection.prepare("UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?1;");
    if (!touch) {
        return tx::rollbackWith(m_connection, touch.failure());
    }
    touch.value().bindInt64(1, playlistId);
    if (const Result<void> ran = touch.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::moveTracksBetweenPlaylists(const QString& sourcePlaylistKey,
                                                              const QString& targetPlaylistKey,
                                                              const QList<qint64>& trackIds)
{
    if (sourcePlaylistKey.isEmpty() || targetPlaylistKey.isEmpty() || sourcePlaylistKey == targetPlaylistKey
        || trackIds.isEmpty()) {
        return tx::notFound(QStringLiteral("nothing to move"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    const qint64 sourceId = lookupId(kPlaylistIdByNumberedKeySql, sourcePlaylistKey);
    const qint64 targetId = lookupId(kPlaylistIdByNumberedKeySql, targetPlaylistKey);
    if (sourceId <= 0 || targetId <= 0) {
        return tx::rollbackWith(m_connection, tx::notFound(QStringLiteral("no playlist with that key")));
    }

    auto nextOrderStatement =
        m_connection.prepare("SELECT COALESCE(MAX(TrackOrder),0)+1 FROM PlaylistTracks WHERE PlaylistId=?1;");
    if (!nextOrderStatement) {
        return tx::rollbackWith(m_connection, nextOrderStatement.failure());
    }
    nextOrderStatement.value().bindInt64(1, targetId);
    // Read and bound as a 32-bit int, as before.
    int nextOrder = nextOrderStatement.value().step().valueOr(false)
        ? static_cast<int>(nextOrderStatement.value().columnInt64(0))
        : 1;

    auto append = m_connection.prepare(kAppendMovedMemberSql);
    auto remove = m_connection.prepare(kRemoveMemberSql);
    if (!append || !remove) {
        return tx::rollbackWith(m_connection, !append ? append.failure() : remove.failure());
    }

    QSet<qint64> uniqueIds;
    int moved = 0;
    for (const qint64 trackId : trackIds) {
        if (trackId <= 0 || uniqueIds.contains(trackId)) {
            continue;
        }
        uniqueIds.insert(trackId);

        append.value().reset();
        append.value().clearBindings();
        append.value().bindInt64(1, targetId);
        append.value().bindInt64(2, nextOrder);
        append.value().bindInt64(3, sourceId);
        append.value().bindInt64(4, trackId);
        if (const Result<void> ran = append.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
        if (m_connection.changes() > 0) {
            ++nextOrder;
        }

        remove.value().reset();
        remove.value().clearBindings();
        remove.value().bindInt64(1, sourceId);
        remove.value().bindInt64(2, trackId);
        if (const Result<void> ran = remove.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
        moved += m_connection.changes();
    }
    if (moved == 0) {
        return tx::rollbackWith(m_connection, tx::notFound(QStringLiteral("no listed song was in the source playlist")));
    }

    auto touch = m_connection.prepare("UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id IN (?1,?2);");
    if (!touch) {
        return tx::rollbackWith(m_connection, touch.failure());
    }
    touch.value().bindInt64(1, sourceId);
    touch.value().bindInt64(2, targetId);
    if (const Result<void> ran = touch.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::renamePlaylist(const QString& playlistKey, const QString& title)
{
    if (playlistKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty playlist key"));
    }
    const QString cleanedTitle = title.isEmpty() ? QStringLiteral("Untitled Playlist") : title;
    auto statement =
        m_connection.prepare("UPDATE Playlists SET Title=?, UpdatedAt=strftime('%s','now') WHERE PlaylistKey=?;");
    if (!statement) {
        return statement.failure();
    }
    statement.value().bindText(1, cleanedTitle);
    statement.value().bindText(2, playlistKey);
    if (const Result<void> ran = statement.value().run(); !ran) {
        return ran;
    }
    if (m_connection.changes() <= 0) {
        return tx::notFound(QStringLiteral("no playlist with that key"));
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::deletePlaylist(const QString& playlistKey)
{
    if (playlistKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty playlist key"));
    }
    auto statement = m_connection.prepare("DELETE FROM Playlists WHERE PlaylistKey=?;");
    if (!statement) {
        return statement.failure();
    }
    statement.value().bindText(1, playlistKey);
    if (const Result<void> ran = statement.value().run(); !ran) {
        return ran;
    }
    if (m_connection.changes() <= 0) {
        return tx::notFound(QStringLiteral("no playlist with that key"));
    }
    return Result<void>::success();
}

} // namespace lmp::core::db
