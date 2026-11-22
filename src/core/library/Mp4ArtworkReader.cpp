#include "core/library/Mp4ArtworkReader.h"

#include "core/library/ArtworkByteSource.h"
#include "core/library/ArtworkPictureCandidate.h"
#include "core/library/BigEndianBytes.h"

#include <array>
#include <cstring>
#include <vector>

namespace lmp::core::artwork {

namespace {

// Bounds for pathological input.
constexpr int MaxMp4AtomsPerLevel = 4096;
constexpr int MaxMp4Depth = 8;

// Atom types are four printable characters; unlike ID3 frame ids they are
// usually lower case and may start with 0xA9 (the copyright sign).
bool looksLikeMp4AtomType(const std::uint8_t* bytes) noexcept
{
    for (int index = 0; index < 4; ++index) {
        const std::uint8_t character = bytes[index];
        if (character != 0xA9 && (character < 0x20 || character > 0x7E)) {
            return false;
        }
    }
    return true;
}

// A `data` atom under `covr`: a four-byte type indicator whose low byte names
// the image format, four reserved bytes, then the picture.
void parseMp4CoverData(ArtworkByteSource& source, std::uint64_t offset, std::uint64_t size,
                       ArtworkPictureCandidate& best)
{
    if (size <= 8) {
        return;
    }
    std::vector<std::uint8_t> payload;
    if (!source.readInto(offset + 8, size - 8, MaxEmbeddedArtworkBytes, payload)) {
        return;
    }
    // MP4 has no picture types, so a cover atom always counts as the front cover.
    best.offer(payload, true);
}

// `meta` is a full box: four version and flag bytes come before its children.
// Some writers leave them out, so fall back when the first child looks wrong.
std::uint64_t mp4MetaChildOffset(ArtworkByteSource& source, std::uint64_t offset, std::uint64_t end) noexcept
{
    std::array<std::uint8_t, 8> header{};
    if (offset + 4 <= end && source.read(offset + 4, header.data(), header.size())) {
        const std::uint32_t atomSize = readBigEndian32(header.data());
        if (atomSize >= 8 && looksLikeMp4AtomType(header.data() + 4)) {
            return offset + 4;
        }
    }
    return offset;
}

bool isType(const std::uint8_t* type, const char* name) noexcept
{
    return std::memcmp(type, name, 4) == 0;
}

} // namespace

void parseMp4Atoms(ArtworkByteSource& source, std::uint64_t offset, std::uint64_t end, int depth,
                   ArtworkPictureCandidate& best)
{
    if (depth > MaxMp4Depth) {
        return;
    }

    std::uint64_t cursor = offset;
    for (int index = 0; index < MaxMp4AtomsPerLevel && cursor + 8 <= end; ++index) {
        std::array<std::uint8_t, 16> header{};
        if (!source.read(cursor, header.data(), 8)) {
            return;
        }

        std::uint64_t atomSize = readBigEndian32(header.data());
        std::uint64_t headerSize = 8;
        if (atomSize == 1) {
            if (!source.read(cursor, header.data(), header.size())) {
                return;
            }
            atomSize = readBigEndian64(header.data() + 8);
            headerSize = 16;
        } else if (atomSize == 0) {
            atomSize = end - cursor; // extends to the end of its parent
        }

        if (atomSize < headerSize || atomSize > end - cursor) {
            return;
        }

        const std::uint8_t* const type = header.data() + 4;
        const std::uint64_t childStart = cursor + headerSize;
        const std::uint64_t childEnd = cursor + atomSize;

        if (isType(type, "moov") || isType(type, "udta") || isType(type, "ilst") || isType(type, "covr")) {
            parseMp4Atoms(source, childStart, childEnd, depth + 1, best);
        } else if (isType(type, "meta")) {
            parseMp4Atoms(source, mp4MetaChildOffset(source, childStart, childEnd), childEnd, depth + 1, best);
        } else if (isType(type, "data")) {
            parseMp4CoverData(source, childStart, atomSize - headerSize, best);
        }

        if (best.hasFrontCover()) {
            return;
        }
        cursor = childEnd;
    }
}

} // namespace lmp::core::artwork
