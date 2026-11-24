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
// single). Start mix, for a remote song where mixes play, and Share, where
// links can be made, sit before Go to artist. Items the mode cannot do for
// this song are left out, not shown disabled. Download joins when it exists.
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
        // A remote song can start a mix of songs like it.
        bool mix = false;
        // A link to the song can be made (Account mode).
        bool share = false;
        // downloadMenuItem: the song's download, "" when songs cannot be
        // kept here; "offer", "done", "pending" or "failed".
        QString download;
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
    static constexpr QLatin1StringView Mix{"mix"};
    static constexpr QLatin1StringView Share{"share"};
    static constexpr QLatin1StringView Download{"download"};
    static constexpr QLatin1StringView RemoveDownload{"remove-download"};
    static constexpr QLatin1StringView RetryDownload{"retry-download"};

    // `playlistKey` is the playlist whose page the song is on, or empty.
    [[nodiscard]] static QList<MenuItemListModel::Item> songMenu(const core::Track& track, const Abilities& abilities,
                                                                 const QString& playlistKey = {});
};

} // namespace lmp::ui
