#pragma once

#include <QColor>
#include <QList>
#include <QString>

#include <optional>

#include "core/db/LibraryQueryTypes.h"

namespace lmp::ui {

// The library's three system playlists, as the current app defines them
// (MainWindow.LibraryHydration.cpp 186-188 and the queries after 300):
// Favourites (liked songs, newest like first), Most Played (played songs,
// most played first) and Recently Added (every song, newest first), with the
// reference's gradient covers (views.css 281-289; playlist-art.ts).
struct SystemPlaylist {
    QString key;
    QString title;
    QString description;
    QString iconName;
    QColor gradientStart;
    QColor gradientEnd;
    // The songs' query; the caller sets paging.
    core::db::TrackQuery query;
};

[[nodiscard]] const QList<SystemPlaylist>& systemPlaylists();
[[nodiscard]] std::optional<SystemPlaylist> systemPlaylist(const QString& key);

} // namespace lmp::ui
