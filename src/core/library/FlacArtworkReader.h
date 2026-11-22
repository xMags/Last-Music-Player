#pragma once

#include <cstdint>

namespace lmp::core::artwork {

class ArtworkByteSource;
struct ArtworkPictureCandidate;

// Offers the PICTURE blocks of the FLAC stream whose "fLaC" marker is at
// `offset` to `best`, stopping at the first front cover or the last block.
void parseFlacMetadata(ArtworkByteSource& source, std::uint64_t offset, ArtworkPictureCandidate& best);

} // namespace lmp::core::artwork
