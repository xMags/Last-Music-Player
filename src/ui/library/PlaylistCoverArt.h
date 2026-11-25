#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include "core/model/Track.h"

namespace lmp::ui {

// A collection's cover from its songs (playlist-art.ts): up to four distinct
// covers in list order. With two or more and `collageAllowed`, a 2x2
// collage (two as a checkerboard, three or four in order); otherwise the
// first cover, or none.
struct PlaylistCoverArt {
    // "collage" or "image".
    QString style = QStringLiteral("image");
    // The single cover ("image"); empty when no song has one.
    QUrl image;
    // Four tile addresses in slot order ("collage").
    QList<QUrl> collage;
};

[[nodiscard]] PlaylistCoverArt playlistCoverArt(const QList<core::Track>& tracks, bool collageAllowed);

} // namespace lmp::ui
