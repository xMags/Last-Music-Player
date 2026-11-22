#pragma once

#include "core/model/Track.h"

#include <QString>

namespace lmp::core {

// The song identities LastMusicState.json and the old home history use,
// built exactly as earlier versions built them. Pure functions, no I/O.

// The old home history's de-duplication key:
//   local file:  "local|" + path + "|" + title + "|" + artist;
//   remote song: "remote-id|" + server id, else "remote-url|" + source
//                address, else "remote|" + title + "|" + artist, or
//                "remote-url|" + path when the title is empty.
// Title and artist go through canonicalQueueText. A song is remote when its
// kind is "remote" or its path or source address is an http(s) address.
[[nodiscard]] QString homeHistoryKey(const Track& track);

// Lower-cases A to Z, keeps letters and digits, and turns every run of other
// characters into one space, with no space at either end.
[[nodiscard]] QString canonicalQueueText(const QString& text);

// catalogSourceKey, or homeHistoryKey when that is empty (no path and no
// address), which is the fallback earlier versions used for the SourceKey of
// such a song.
[[nodiscard]] QString legacyCatalogSourceKey(const Track& track);

// The "LastTrack" value for `track`: its path when it has one that is not a
// web address, else "remote-id:" + its server id, else "source:" + its
// legacyCatalogSourceKey.
[[nodiscard]] QString lastTrackValue(const Track& track);

// Whether `candidate` is the song a saved "LastTrack" value names: the same
// path, or the server id after "remote-id:", or the legacyCatalogSourceKey
// after "source:". An empty value names nothing.
[[nodiscard]] bool matchesLastTrackValue(const Track& candidate, const QString& lastTrack);

} // namespace lmp::core
