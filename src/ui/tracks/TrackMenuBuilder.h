#pragma once

#include <QList>
#include <QString>

#include "core/model/Track.h"
#include "ui/menus/MenuItemListModel.h"

namespace lmp::ui {

// The items of a song's menu, in the reference client's order (track-menu.ts):
// Play now, Play next, Add to queue; Add to playlist, the heart; Remove from
// playlist on a playlist's own page, with the current app's Move up and Move
// down where the page shows the playlist's own order; Go to artist, Go to album (not for a
// single). Items the mode cannot do for this song are left out, not shown
// disabled. Download, Start mix and Share join when their features exist.
//
// "Add to playlist" opens a second menu: New playlist, then the playlists.
class TrackMenuBuilder {
public:
    // What the song may do here, as TrackActions answers it.
    struct Abilities {
        bool play = true;
        bool keep = true;
        // On a playlist's page in its own order: the song can go up or down
        // one place (the current app's Move up and Move down).
        bool moveUp = false;
        bool moveDown = false;
    };

    // Item ids.
    static constexpr QLatin1StringView Play{"play"};
    static constexpr QLatin1StringView PlayNext{"next"};
    static constexpr QLatin1StringView Queue{"queue"};
    static constexpr QLatin1StringView AddToPlaylist{"playlist"};
    static constexpr QLatin1StringView Like{"like"};
    static constexpr QLatin1StringView RemoveFromPlaylist{"remove-from-playlist"};
    static constexpr QLatin1StringView MoveUp{"move-up"};
    static constexpr QLatin1StringView MoveDown{"move-down"};
    static constexpr QLatin1StringView Artist{"artist"};
    static constexpr QLatin1StringView Album{"album"};
    static constexpr QLatin1StringView NewPlaylist{"new-playlist"};
    // A playlist item's id is this prefix and the playlist's key.
    static constexpr QLatin1StringView PlaylistPrefix{"to-playlist:"};

    // `playlistKey` is the playlist whose page the song is on, or empty.
    [[nodiscard]] static QList<MenuItemListModel::Item> songMenu(const core::Track& track, const Abilities& abilities,
                                                                 const QString& playlistKey = {});
    // New playlist, then one item per playlist (title; key in sourceUrl).
    [[nodiscard]] static QList<MenuItemListModel::Item> playlistMenu(const QList<core::Track>& playlists);
};

} // namespace lmp::ui
