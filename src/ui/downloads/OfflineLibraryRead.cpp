#include "ui/downloads/OfflineLibraryRead.h"

#include "core/db/AccountRepository.h"
#include "core/db/Database.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/TrackActions.h"

namespace lmp::ui {

OfflineLibraryRead readOfflineLibrary(core::db::Database& database, const QList<core::db::TrackQuery>& systemQueries,
                                      bool accountPlaylists)
{
    // The same wiring as the main connection's queries: account playlists'
    // songs come from the account tables.
    const core::db::AccountRepository accounts(database);
    core::db::LibraryQueries library(database, &accounts);
    core::db::GroupQueries groups(database);

    OfflineLibraryRead read;
    for (const core::Track& track : library.loadTracks()) {
        if (track.isRemote()) {
            read.remoteSongs.append(track);
        }
    }
    for (const core::db::TrackQuery& query : systemQueries) {
        read.systemSongs.append(library.loadTracksForQuery(query));
    }
    read.playlists = core::TrackActions::ownPlaylists(groups.loadPlaylists(), accountPlaylists);
    for (const core::Track& playlist : std::as_const(read.playlists)) {
        core::db::TrackQuery query;
        query.groupKind = QStringLiteral("playlist");
        query.groupKey = playlist.sourceUrl;
        read.playlistSongs.append(library.loadTracksForQuery(query));
    }
    read.history = library.loadHistoryTracks();
    return read;
}

} // namespace lmp::ui
