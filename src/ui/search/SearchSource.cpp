#include "ui/search/SearchSource.h"

#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"

namespace lmp::ui {

LocalSearchSource::LocalSearchSource(core::db::LibraryQueries& library, core::db::GroupQueries& groups)
    : m_library(library)
    , m_groups(groups)
{
}

void LocalSearchSource::search(const QString& query, Done done)
{
    SearchHits hits;
    core::db::TrackQuery songs;
    songs.searchText = query;
    songs.sort = QStringLiteral("DateAdded");
    songs.includeRemote = true;
    songs.limit = SongLimit;
    hits.songs = m_library.loadTrackPage(songs).tracks;
    for (const core::Track& playlist : m_groups.loadPlaylists()) {
        // The account's playlists come with account sync (P07), as elsewhere.
        if (playlist.origin == QLatin1StringView("account")) {
            continue;
        }
        if (playlist.title.contains(query, Qt::CaseInsensitive)) {
            hits.playlists.append(playlist);
        }
    }
    done(std::move(hits));
}

} // namespace lmp::ui
