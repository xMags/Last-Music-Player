#include "core/library/FlacArtworkReader.h"

#include "core/library/ArtworkByteSource.h"
#include "core/library/ArtworkPictureCandidate.h"
#include "core/library/BigEndianBytes.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

namespace lmp::core::artwork {

namespace {

// A real stream has a handful of metadata blocks; this only stops
// pathological input.
constexpr int MaxFlacBlocks = 1024;

constexpr std::uint8_t PictureBlockType = 6;

// Largest PICTURE block read, the same bound as a whole ID3 tag.
constexpr std::uint64_t MaxPictureBlockBytes = MaxEmbeddedArtworkBytes + (1u * 1024u * 1024u);

// METADATA_BLOCK_PICTURE: picture type, length-prefixed media type and
// description, four fixed geometry fields, then length-prefixed data.
void parseFlacPictureBlock(const std::vector<std::uint8_t>& block, ArtworkPictureCandidate& best)
{
    constexpr std::size_t FixedFields = 4 + 4 + 4 + 4 + 4 + 4 + 4;
    if (block.size() < FixedFields) {
        return;
    }

    const std::uint32_t pictureType = readBigEndian32(block.data());
    std::size_t cursor = 4;

    const auto readLengthPrefixed = [&block, &cursor](std::size_t& length) noexcept {
        if (block.size() - cursor < 4) {
            return false;
        }
        const std::uint32_t declared = readBigEndian32(block.data() + cursor);
        cursor += 4;
        if (declared > block.size() - cursor) {
            return false;
        }
        length = declared;
        return true;
    };

    std::size_t mimeLength = 0;
    if (!readLengthPrefixed(mimeLength)) {
        return;
    }
    const std::size_t mimeStart = cursor;
    cursor += mimeLength;

    std::size_t descriptionLength = 0;
    if (!readLengthPrefixed(descriptionLength)) {
        return;
    }
    cursor += descriptionLength;

    // Width, height, colour depth, indexed colour count.
    if (block.size() - cursor < 16) {
        return;
    }
    cursor += 16;

    std::size_t dataLength = 0;
    if (!readLengthPrefixed(dataLength)) {
        return;
    }

    // "-->" as the media type means the payload is a link, not a picture.
    if (mimeLength == 3 && std::memcmp(block.data() + mimeStart, "-->", 3) == 0) {
        return;
    }

    const auto dataStart = block.begin() + static_cast<std::ptrdiff_t>(cursor);
    best.offer(std::vector<std::uint8_t>(dataStart, dataStart + static_cast<std::ptrdiff_t>(dataLength)),
               pictureType == PictureTypeFrontCover);
}

} // namespace

void parseFlacMetadata(ArtworkByteSource& source, std::uint64_t offset, ArtworkPictureCandidate& best)
{
    std::uint64_t cursor = offset + 4; // past the "fLaC" marker
    for (int index = 0; index < MaxFlacBlocks; ++index) {
        std::array<std::uint8_t, 4> header{};
        if (!source.read(cursor, header.data(), header.size())) {
            return;
        }

        const bool isLast = (header[0] & 0x80u) != 0;
        const auto blockType = static_cast<std::uint8_t>(header[0] & 0x7Fu);
        const std::uint32_t blockSize = readBigEndian24(header.data() + 1);
        cursor += 4;

        if (blockType == PictureBlockType) {
            std::vector<std::uint8_t> block;
            if (!source.readInto(cursor, blockSize, MaxPictureBlockBytes, block)) {
                return;
            }
            parseFlacPictureBlock(block, best);
            if (best.hasFrontCover()) {
                return;
            }
        }

        if (isLast || !source.contains(cursor, blockSize)) {
            return;
        }
        cursor += blockSize;
    }
}

} // namespace lmp::core::artwork
