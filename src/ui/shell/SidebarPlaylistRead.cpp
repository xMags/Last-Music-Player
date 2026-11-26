#include "ui/shell/SidebarPlaylistRead.h"

#include "core/db/AccountRepository.h"
#include "core/db/Database.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/DetailSortPolicy.h"
#include "core/library/TrackActions.h"

namespace lmp::ui {

SidebarPlaylistRead readSidebarPlaylists(core::db::Database& database, bool accountPlaylists, int limit, int coverSongs)
{
    // The same wiring as the main connection's queries.
    const core::db::AccountRepository accounts(database);
    core::db::LibraryQueries library(database, &accounts);
    core::db::GroupQueries groups(database);

    SidebarPlaylistRead read;
    for (const core::Track& playlist : core::TrackActions::ownPlaylists(groups.loadPlaylists(), accountPlaylists)) {
        if (read.playlists.size() == limit) {
            break;
        }
        // The first songs in the playlist's own order are enough to find
        // four distinct covers in almost every playlist.
        core::db::TrackQuery query;
        query.groupKind = QStringLiteral("playlist");
        query.groupKey = playlist.sourceUrl;
        query.sort = core::detailSortQueryValue(core::DetailSort::Curated);
        query.limit = coverSongs;
        read.playlists.append(playlist);
        read.songs.append(library.loadTrackPage(query).tracks);
    }
    return read;
}

} // namespace lmp::ui
