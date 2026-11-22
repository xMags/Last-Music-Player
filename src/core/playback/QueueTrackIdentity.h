#pragma once

#include "core/model/Track.h"

#include <QString>

namespace lmp::core {

// What makes two queue entries the same song, for de-duplication and for
// telling whether a removed entry is already back. A remote song is its
// server id; a local file is its path, compared exactly as stored. The two
// kinds carry different prefixes so they can never collide. Empty when the
// song has neither: the queue drops such songs, as it drops songs without
// an id.
[[nodiscard]] QString queueTrackIdentity(const Track& track);

} // namespace lmp::core
