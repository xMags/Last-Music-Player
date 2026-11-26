#pragma once

#include <QList>

#include "core/db/LibraryQueryTypes.h"
#include "core/model/Track.h"

namespace lmp::core::db {
class Database;
} // namespace lmp::core::db

namespace lmp::ui {

// What the Offline page's collection tabs read from the library: the songs
// that downloads are matched against, the system playlists' and the user's
// playlists' songs, and the play history. Read on the database reader's
// thread, then cut down to the downloaded songs on the GUI thread.
struct OfflineLibraryRead {
    // The library's remote songs (a download is matched to one by key).
    QList<core::Track> remoteSongs;
    // Each system playlist's songs, in the order of the queries asked for.
    QList<QList<core::Track>> systemSongs;
    // The user's own playlists, and each one's songs in the same order.
    QList<core::Track> playlists;
    QList<QList<core::Track>> playlistSongs;
    // The play history, newest first.
    QList<core::Track> history;
};

// Reads everything above from `database`. `systemQueries` are the system
// playlists' queries; `accountPlaylists` says whether the account's playlists
// count as the user's (TrackActions::ownPlaylists). Runs on any one thread
// that owns `database`; it touches nothing else.
[[nodiscard]] OfflineLibraryRead readOfflineLibrary(core::db::Database& database,
                                                    const QList<core::db::TrackQuery>& systemQueries,
                                                    bool accountPlaylists);

} // namespace lmp::ui
