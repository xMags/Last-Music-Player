#pragma once

#include <QList>

#include "core/model/Track.h"

namespace lmp::core::db {
class Database;
} // namespace lmp::core::db

namespace lmp::ui {

// What the sidebar's playlist list reads from the library: the user's own
// playlists (at most `limit`) and, for each, its first `coverSongs` songs in
// its own order, where its cover is looked for. Read on the database reader's
// thread; the covers are worked out on the GUI thread.
struct SidebarPlaylistRead {
    QList<core::Track> playlists;
    // songs[i] belongs to playlists[i].
    QList<QList<core::Track>> songs;
};

// `accountPlaylists`: whether the account's playlists count as the user's
// (TrackActions::ownPlaylists). Touches nothing but `database`.
[[nodiscard]] SidebarPlaylistRead readSidebarPlaylists(core::db::Database& database, bool accountPlaylists, int limit,
                                                       int coverSongs);

} // namespace lmp::ui
