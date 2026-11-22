#pragma once

#include "core/db/Database.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace lmp::core::db {

// The cards of the library: albums, playlists, artists and genres. Each card
// is a Track standing for its group, filled the way earlier versions filled
// their card rows: title, artist (the caption: album artist, description or
// a count), artworkUrl, trackCount, sourceKind ("album", "album-collection",
// "playlist", "artist" or "genre"), sourceUrl (the key that opens the group),
// sourceLabel, and for collections rowId and origin.
//
// A query that fails returns the cards read so far, usually none, as earlier
// versions did.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; that Database must outlive it.
class GroupQueries {
public:
    explicit GroupQueries(Database& database);

    // Album collections first (most recently changed first), then the albums
    // derived from song tags, largest first. Songs of imported collections
    // are left out of the derived albums.
    [[nodiscard]] QList<Track> loadAlbums();
    // Playlists on this PC, then the active account's synced playlists.
    [[nodiscard]] QList<Track> loadPlaylists();
    // loadPlaylists with account playlists first, then by title; at most
    // `limit` cards, none for 0 or less.
    [[nodiscard]] QList<Track> loadRecentPlaylists(int limit);
    // Largest first. With a scope key, the artists followed in that scope
    // (see FollowedArtistRepository) are merged in: a card that matches one
    // by ArtistIdentity is marked followed, takes the follow time as its
    // added time and gains a "Following" suffix in its caption (after two
    // spaces, a U+00B7 middle dot and two spaces); every other followed
    // artist is appended as a card of its own, captioned "Following", in
    // the order most recently followed first.
    [[nodiscard]] QList<Track> loadArtists(const QString& followedScopeKey = {});
    [[nodiscard]] QList<Track> loadGenres();
    // Genre names ranked by total listening (SUM(PlayCount)), falling back to
    // genre size when there is no play history yet. Untagged songs and broad
    // media categories are left out. Empty for 0 or less.
    [[nodiscard]] QStringList loadTopGenres(int limit);

private:
    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
