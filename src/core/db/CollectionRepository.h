#pragma once

#include "core/async/Result.h"
#include "core/db/Database.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>
#include <QtGlobal>

namespace lmp::core::db {

// Album collections and playlists on this PC, and the songs in them. A
// collection travels as a Track the way earlier versions passed it: title,
// artist (the album artist, or the playlist description), artworkUrl,
// sourceKind, origin (a playlist's provider), sourceUrl and sourceLabel.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; that Database must outlive it.
//
// An operation that earlier versions reported as false or 0 returns a
// Failure: FailureKind::NotFound when there was nothing to act on (an empty
// key, a missing collection, no row changed), FailureKind::Storage when
// SQLite refused. An operation that earlier versions skipped silently still
// succeeds without writing.
class CollectionRepository {
public:
    explicit CollectionRepository(Database& database);

    // A new, empty collection made by hand, under a fresh "manual|..." key.
    // An empty title becomes "Untitled Album" or "Untitled Playlist".
    [[nodiscard]] Result<qint64> createAlbumCollection(const QString& title);
    [[nodiscard]] Result<qint64> createPlaylist(const QString& title);

    // Inserts or updates the collection with this key and returns its id.
    // Artwork that is not safe to keep is stored empty unless the album's
    // source kind (or the playlist's origin) is "manual".
    [[nodiscard]] Result<qint64> upsertAlbumCollection(const Track& album, const QString& albumKey);
    [[nodiscard]] Result<qint64> upsertPlaylist(const Track& playlist, const QString& playlistKey);

    // Replaces the members, numbered 1, 2, 3 in list order; ids that are not
    // positive are skipped. A non-positive collection id does nothing.
    [[nodiscard]] Result<void> replaceAlbumCollectionTracks(qint64 albumId, const QList<qint64>& trackIds);
    [[nodiscard]] Result<void> replacePlaylistTracks(qint64 playlistId, const QList<qint64>& trackIds);

    // Deletes the collection and its links. For an imported (not manual)
    // album, its remote songs that no other album or playlist holds go too.
    [[nodiscard]] Result<void> deleteAlbumCollection(const QString& albumKey);

    // Stores `trackIds` as the playlist's whole order, then succeeds only if
    // the stored order reads back exactly as `trackIds`.
    [[nodiscard]] Result<void> reorderPlaylistTracks(const QString& playlistKey, const QList<qint64>& trackIds);
    [[nodiscard]] Result<void> addTrackToPlaylist(const QString& playlistKey, qint64 trackId);
    // See addTracksToPlaylistTransactional.
    [[nodiscard]] Result<void> addTracksToPlaylist(const QString& playlistKey, const QList<qint64>& trackIds);
    // Succeeds even when the song was not in the playlist, as before.
    [[nodiscard]] Result<void> removeTrackFromPlaylist(const QString& playlistKey, qint64 trackId);
    // All or nothing; fails when no listed song was in the playlist.
    [[nodiscard]] Result<void> removeTracksFromPlaylist(const QString& playlistKey, const QList<qint64>& trackIds);
    // Appends the listed songs of the source playlist to the target, in list
    // order, and removes them from the source. All or nothing; fails when the
    // keys are equal or nothing moved.
    [[nodiscard]] Result<void> moveTracksBetweenPlaylists(const QString& sourcePlaylistKey,
                                                          const QString& targetPlaylistKey,
                                                          const QList<qint64>& trackIds);
    // An empty title becomes "Untitled Playlist". Fails when no row changed.
    [[nodiscard]] Result<void> renamePlaylist(const QString& playlistKey, const QString& title);
    // Its member links go with it. Fails when no row changed.
    [[nodiscard]] Result<void> deletePlaylist(const QString& playlistKey);

private:
    [[nodiscard]] Result<void> replaceMembers(const char* deleteSql,
                                              const char* insertSql,
                                              const char* touchSql,
                                              qint64 collectionId,
                                              const QList<qint64>& trackIds);
    // The id the query finds for `key`, or 0 when it finds none or fails.
    [[nodiscard]] qint64 lookupId(const char* sql, const QString& key);

    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
