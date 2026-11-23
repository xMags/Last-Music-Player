#pragma once

#include "core/db/LibraryQueryTypes.h"
#include "ui/tracks/TrackListModel.h"

namespace lmp::core::db {
class LibraryQueries;
}

namespace lmp::ui {

// A song list read from the library a page at a time (loadTrackPage with
// the query's filter, sort, group and search, plus OFFSET and LIMIT).
class QueryTrackSource final : public TrackPageSource {
public:
    // The queries are owned by the composition root and outlive this.
    QueryTrackSource(core::db::LibraryQueries& library, core::db::TrackQuery query);

    [[nodiscard]] int count() override;
    [[nodiscard]] QList<core::Track> page(int offset, int limit) override;

private:
    core::db::LibraryQueries& m_library;
    core::db::TrackQuery m_query;
};

} // namespace lmp::ui
