#pragma once

#include <cstdint>

namespace lmp::core::artwork {

class ArtworkByteSource;
struct ArtworkPictureCandidate;

// Walks the MP4 atoms between `offset` and `end` (moov, udta, meta, ilst,
// covr) and offers each cover `data` atom to `best`. `depth` is the nesting
// level of these atoms; 0 for the top level of the file.
void parseMp4Atoms(ArtworkByteSource& source, std::uint64_t offset, std::uint64_t end, int depth,
                   ArtworkPictureCandidate& best);

} // namespace lmp::core::artwork
