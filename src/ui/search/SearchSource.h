#pragma once

#include <QList>
#include <QString>

#include <functional>

#include "core/model/Track.h"

namespace lmp::core::db {
class LibraryQueries;
class GroupQueries;
} // namespace lmp::core::db

namespace lmp::ui {

// What a search found: songs, and playlists whose name matches.
struct SearchHits {
    QList<core::Track> songs;
    QList<core::Track> playlists;
};

// Somewhere a search can look: the local library now; the catalogue and the
// API key provider join in P08 and P09 behind the same interface. A source
// answers through `done`, on the GUI thread, now or later; the caller drops
// answers that arrive for a search that is no longer current.
class SearchSource {
public:
    using Done = std::function<void(SearchHits)>;

    virtual ~SearchSource() = default;
    virtual void search(const QString& query, Done done) = 0;

protected:
    SearchSource() = default;
    SearchSource(const SearchSource&) = default;
    SearchSource& operator=(const SearchSource&) = default;
};

// The local library, as the current app searched it (RunHomeSearchNowAsync):
// up to 60 songs matching title, artist or album, newest first, including
// remote songs that live in the library; and the user's playlists on this
// PC whose name contains the query. Answers at once.
class LocalSearchSource final : public SearchSource {
public:
    static constexpr int SongLimit = 60;

    // Both outlive this object.
    LocalSearchSource(core::db::LibraryQueries& library, core::db::GroupQueries& groups);

    void search(const QString& query, Done done) override;

private:
    core::db::LibraryQueries& m_library;
    core::db::GroupQueries& m_groups;
};

} // namespace lmp::ui
