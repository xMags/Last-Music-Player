#pragma once

#include "core/db/AccountPlaylistPage.h"
#include "core/db/Database.h"
#include "core/db/LibraryQueryTypes.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>

namespace lmp::core::db {

// Song listings: the library, history and most played, a page of the songs
// list (flat, by group, or one collection), recently added, and the totals.
// Every query reads the EffectiveTracks view, so account songs show up while
// an account is the active context, except the collection pages, which read
// Tracks directly.
//
// A query that fails returns what it read so far, usually nothing, as earlier
// versions did; nothing here reports a Failure.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; the Database, and the account playlist source when
// given, must outlive it.
class LibraryQueries {
public:
    // Without an account playlist source, an account playlist page is empty.
    explicit LibraryQueries(Database& database, const AccountPlaylistTrackSource* accountPlaylists = nullptr);

    // Newest first.
    [[nodiscard]] QList<Track> loadTracks(bool includeRemote = true, bool activeOnly = true);
    // Most recently played first; only songs played at least once.
    [[nodiscard]] QList<Track> loadHistoryTracks(bool includeRemote = true, bool activeOnly = true);
    // Most played first; only songs played at least once.
    [[nodiscard]] QList<Track> loadMostPlayedTracks(bool includeRemote = true, bool activeOnly = true);
    // Remote songs with an exact play time from an older build, newest first.
    [[nodiscard]] QList<HistoryImportCandidate> loadLegacyRemoteHistoryImportCandidates();

    [[nodiscard]] TrackPage loadTrackPage(const TrackQuery& query);
    // Every row of the query, ignoring its offset and limit.
    [[nodiscard]] QList<Track> loadTracksForQuery(const TrackQuery& query);
    // The songs of one album, artist, genre, album collection or playlist;
    // empty for an empty key or another kind.
    [[nodiscard]] QList<Track> loadTracksForGroup(const QString& groupKind, const QString& groupKey);
    // Newest first, only songs with an added time; 0 or less means no limit.
    [[nodiscard]] QList<Track> loadRecentlyAddedTracks(int limit);

    [[nodiscard]] LibraryStats libraryStats();
    // Active songs with any play count or history position.
    [[nodiscard]] QList<PlaybackStat> loadPlaybackStats();

private:
    enum class CollectionTable {
        Albums,
        Playlists,
    };

    [[nodiscard]] QList<Track> loadSimpleList(const char* sql, bool includeRemote, bool activeOnly);
    [[nodiscard]] TrackPage loadCollectionPage(const TrackQuery& query, CollectionTable table);
    [[nodiscard]] TrackPage loadFlatPage(const TrackQuery& query);

    SqliteConnection& m_connection;
    // The account of the active library context in Account mode; empty
    // otherwise.
    [[nodiscard]] QString activeAccountOwner() const;

    const AccountPlaylistTrackSource* m_accountPlaylists;
};

} // namespace lmp::core::db
