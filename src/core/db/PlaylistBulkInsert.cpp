#include "core/db/PlaylistBulkInsert.h"

#include "core/db/SqlTransaction.h"

namespace lmp::core::db {

namespace tx = sql_transaction;

Result<void> addTracksToPlaylistTransactional(SqliteConnection& connection,
                                              const QString& playlistKey,
                                              const QList<qint64>& trackIds)
{
    if (!connection.isOpen() || playlistKey.isEmpty() || trackIds.isEmpty()) {
        return tx::notFound(QStringLiteral("nothing to add"));
    }
    for (const qint64 trackId : trackIds) {
        if (trackId <= 0) {
            return tx::notFound(QStringLiteral("track id is not positive"));
        }
    }

    if (const Result<void> begun = connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    auto playlist = connection.prepare("SELECT Id FROM Playlists WHERE PlaylistKey=?1;");
    if (!playlist) {
        return tx::rollbackWith(connection, playlist.failure());
    }
    playlist.value().bindText(1, playlistKey);
    const Result<bool> found = playlist.value().step();
    if (!found) {
        return tx::rollbackWith(connection, found.failure());
    }
    if (!found.value()) {
        return tx::rollbackWith(connection, tx::notFound(QStringLiteral("no playlist with that key")));
    }
    const qint64 playlistId = playlist.value().columnInt64(0);

    auto order = connection.prepare("SELECT COALESCE(MAX(TrackOrder),0)+1 FROM PlaylistTracks WHERE PlaylistId=?1;");
    if (!order) {
        return tx::rollbackWith(connection, order.failure());
    }
    order.value().bindInt64(1, playlistId);
    if (!order.value().step().valueOr(false)) {
        return tx::rollbackWith(connection, tx::storageFailure(connection, QStringLiteral("next order")));
    }
    // Read and bound as a 32-bit int, as before.
    int nextOrder = static_cast<int>(order.value().columnInt64(0));

    auto insert =
        connection.prepare("INSERT OR IGNORE INTO PlaylistTracks (PlaylistId,TrackId,TrackOrder) VALUES (?1,?2,?3);");
    if (!insert) {
        return tx::rollbackWith(connection, insert.failure());
    }
    for (const qint64 trackId : trackIds) {
        insert.value().reset();
        insert.value().clearBindings();
        insert.value().bindInt64(1, playlistId);
        insert.value().bindInt64(2, trackId);
        insert.value().bindInt64(3, nextOrder);
        if (const Result<void> ran = insert.value().run(); !ran) {
            return tx::rollbackWith(connection, ran.failure());
        }
        if (connection.changes() > 0) {
            ++nextOrder;
        }
    }

    auto touch = connection.prepare("UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?1;");
    if (!touch) {
        return tx::rollbackWith(connection, touch.failure());
    }
    touch.value().bindInt64(1, playlistId);
    if (const Result<void> ran = touch.value().run(); !ran) {
        return tx::rollbackWith(connection, ran.failure());
    }

    if (const Result<void> committed = connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(connection, committed.failure());
    }
    return Result<void>::success();
}

} // namespace lmp::core::db
