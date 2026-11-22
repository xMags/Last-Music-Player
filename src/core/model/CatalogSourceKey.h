#pragma once

#include "core/model/Track.h"

#include <QString>

namespace lmp::core {

// The SourceKey a song's row has in the Tracks table, built as earlier
// versions built it, so plays and positions land on the rows they wrote:
//   local file:  "local|" + the path with A to Z lower-cased;
//   remote song: "remote|" + origin lower-cased (or "provider" when empty)
//                + "|" + the source address, or the file path when it is an
//                http(s) address and no source address is known.
// A song counts as remote when its kind is "remote" or its file path is an
// http(s) address.
//
// Empty when neither a path nor an address is known. Earlier versions fell
// back to a title-and-artist key there; such songs cannot be played from a
// file, and the fallback is left for the remote playback work.
[[nodiscard]] QString catalogSourceKey(const Track& track);

} // namespace lmp::core
