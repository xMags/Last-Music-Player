#pragma once

#include "core/db/SqliteConnection.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>

#include <string>

// Reading Tracks rows into Track values, and the SQL pieces the track
// listings share. The SQL text is copied character for character from earlier
// versions.
namespace lmp::core::db::track_rows {

// Column order is load-bearing: trackFromStatement reads these by index,
// so anything appended here has to be appended to both lists and to any
// query that selects extra columns after them.
inline constexpr char kTrackColumns[] =
    "Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId";

inline constexpr char kJoinedTrackColumns[] =
    "t.Id, t.SourceKind, t.Provider, t.SourceUrl, t.FilePath, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked, t.RemoteId, t.LastPositionSeconds, '', t.RemoteCatalogId, t.AlbumCatalogId, t.ArtistCatalogId";

// How many columns kTrackColumns selects; extra columns follow from here.
inline constexpr int kTrackColumnCount = 21;

// The current row, selected with kTrackColumns or kJoinedTrackColumns first.
[[nodiscard]] Track trackFromStatement(const SqliteStatement& statement);

// Steps through every row, numbering the tracks from `firstIndex`. Stops at
// the first error, keeping the rows read so far, as earlier versions did.
[[nodiscard]] QList<Track> readTracks(SqliteStatement& statement, int firstIndex = 1);

// The file path a row stores: none for a remote song, and a local path with
// any obsolete server credential removed.
[[nodiscard]] QString storedFilePathFor(const Track& track, const QString& sourceKind);
[[nodiscard]] QString sourceLabelFor(const QString& sourceKind);

// "Local", "Remote" or "Mixed" for a group with `remoteCount` of `count`
// songs from a server.
[[nodiscard]] QString mixLabelFor(int remoteCount, int count);

// "1 song", "2 songs".
[[nodiscard]] QString songCountText(int count);

[[nodiscard]] std::string trackOrderClause(const QString& filter, const QString& sort);
[[nodiscard]] std::string trackFilterClause(const QString& filter);
// Binds the group key as ?3.
[[nodiscard]] std::string groupFilterClause(const QString& groupKind);

// "%text%" with LIKE wildcards and the escape character escaped by "!".
[[nodiscard]] QString escapedLikePattern(const QString& text);

} // namespace lmp::core::db::track_rows
