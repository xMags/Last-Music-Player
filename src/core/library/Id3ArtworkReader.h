#pragma once

#include <cstdint>

namespace lmp::core::artwork {

class ArtworkByteSource;
struct ArtworkPictureCandidate;

// The header of an ID3v2 tag.
struct Id3TagLayout {
    std::uint8_t majorVersion = 0;
    std::uint8_t flags = 0;
    std::uint32_t bodySize = 0;
    // Header, body and the optional footer: where the audio starts.
    std::uint64_t totalSize = 0;
};

// Reads an ID3v2.2, 2.3 or 2.4 tag header at `offset`. False when there is
// none or it is malformed.
[[nodiscard]] bool tryReadId3TagLayout(ArtworkByteSource& source, std::uint64_t offset, Id3TagLayout& layout) noexcept;

// Offers every picture frame (APIC, or PIC in ID3v2.2) of the tag at the start
// of the file to `best`, stopping at the first front cover. Undoes tag and
// frame unsynchronisation, skips extended headers and data length
// indicators, and ignores compressed or encrypted frames.
void parseId3Tag(ArtworkByteSource& source, const Id3TagLayout& layout, ArtworkPictureCandidate& best);

} // namespace lmp::core::artwork
