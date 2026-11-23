#include "ui/tracks/QueryTrackSource.h"

#include <utility>

#include "core/db/LibraryQueries.h"

namespace lmp::ui {

QueryTrackSource::QueryTrackSource(core::db::LibraryQueries& library, core::db::TrackQuery query)
    : m_library(library)
    , m_query(std::move(query))
{
}

int QueryTrackSource::count()
{
    core::db::TrackQuery probe = m_query;
    probe.offset = 0;
    probe.limit = 1;
    return m_library.loadTrackPage(probe).totalCount;
}

QList<core::Track> QueryTrackSource::page(int offset, int limit)
{
    core::db::TrackQuery query = m_query;
    query.offset = offset;
    query.limit = limit;
    return m_library.loadTrackPage(query).tracks;
}

} // namespace lmp::ui
