#pragma once

#include <QString>

namespace lmp::core {

struct Track;

// A song that plainly has no album page of its own (the reference client's
// single-release.ts): no album named (and no album id), an album named
// "<title> - Single", or an album named like the song itself once brackets,
// featured artists and punctuation are set aside. Its menu offers no
// "Go to album".
[[nodiscard]] bool looksLikeSingle(const Track& track);

} // namespace lmp::core
