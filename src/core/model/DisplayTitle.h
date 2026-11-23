#pragma once

#include <QString>

namespace lmp::core {

// Display-only song titles (the reference client's display-title.ts). Video
// uploads carry decoration such as "Song (Official Music Video)",
// "Song | Channel | Season 3" or "Song - Lyrical #trending"; film credits
// ("(From "Film")", "(Movie: Film)"), featured artists ("(feat. X)", "ft. X"),
// producer credits and soundtrack labels are dropped as well. Markers that
// name a different recording (remix, live, cover, acoustic, ...) stay. The
// stored title is never changed: likes, history and matching keep the raw
// value; only what is shown changes. A title that would come out empty is
// shown as it was.
[[nodiscard]] QString displayTitle(const QString& title);

// An album name as shown: film credits, featured artists and soundtrack
// labels in brackets dropped, and a trailing " - Single". Everything else
// that names the release, such as "(Deluxe Edition)" or " - EP", stays.
[[nodiscard]] QString displayAlbumTitle(const QString& album);

} // namespace lmp::core
