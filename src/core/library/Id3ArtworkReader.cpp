#include "core/library/Id3ArtworkReader.h"

#include "core/library/ArtworkByteSource.h"
#include "core/library/ArtworkPictureCandidate.h"
#include "core/library/BigEndianBytes.h"

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <vector>

namespace lmp::core::artwork {

namespace {

// A real tag holds tens of frames; this only stops pathological input.
constexpr int MaxId3Frames = 4096;

// A tag large enough for the biggest accepted picture plus the text frames.
constexpr std::uint64_t MaxId3TagBytes = MaxEmbeddedArtworkBytes + (1u * 1024u * 1024u);

// ID3v2 sizes are "syncsafe": seven significant bits per byte, so an encoded
// size can never contain a false MPEG frame sync.
bool tryReadSyncSafe32(const std::uint8_t* bytes, std::uint32_t& value) noexcept
{
    std::uint32_t result = 0;
    for (int index = 0; index < 4; ++index) {
        if ((bytes[index] & 0x80u) != 0) {
            return false;
        }
        result = (result << 7) | bytes[index];
    }
    value = result;
    return true;
}

bool looksLikeId3FrameId(const std::uint8_t* bytes, std::size_t length) noexcept
{
    for (std::size_t index = 0; index < length; ++index) {
        const std::uint8_t character = bytes[index];
        const bool upper = character >= 'A' && character <= 'Z';
        const bool digit = character >= '0' && character <= '9';
        if (!upper && !digit) {
            return false;
        }
    }
    return true;
}

// FF 00 pairs are inserted so a tag never contains a byte sequence an MPEG
// decoder would take for a frame sync. Collapse them back.
void removeUnsynchronisation(std::vector<std::uint8_t>& bytes)
{
    std::size_t write = 0;
    for (std::size_t read = 0; read < bytes.size(); ++read) {
        bytes[write++] = bytes[read];
        if (bytes[read] == 0xFF && read + 1 < bytes.size() && bytes[read + 1] == 0x00) {
            ++read;
        }
    }
    bytes.resize(write);
}

// Length of the NUL terminator of a string in an ID3 text encoding, or 0 for
// an unknown encoding.
std::size_t id3TerminatorSize(std::uint8_t encoding) noexcept
{
    switch (encoding) {
    case 0: // ISO-8859-1
    case 3: // UTF-8
        return 1;
    case 1: // UTF-16 with byte order mark
    case 2: // UTF-16BE
        return 2;
    default:
        return 0;
    }
}

// The offset just past the terminator, or `end` when the string is not
// terminated (which makes the frame unusable).
std::size_t skipId3String(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::size_t end,
                          std::size_t terminatorSize) noexcept
{
    if (terminatorSize == 1) {
        for (std::size_t cursor = offset; cursor < end; ++cursor) {
            if (bytes[cursor] == 0x00) {
                return cursor + 1;
            }
        }
        return end;
    }

    // UTF-16 terminators are two zero bytes on an even boundary relative to
    // the start of the string.
    for (std::size_t cursor = offset; cursor + 1 < end; cursor += 2) {
        if (bytes[cursor] == 0x00 && bytes[cursor + 1] == 0x00) {
            return cursor + 2;
        }
    }
    return end;
}

std::vector<std::uint8_t> tail(const std::vector<std::uint8_t>& bytes, std::size_t from, std::size_t end)
{
    return std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(from),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(end));
}

// APIC (ID3v2.3/2.4): encoding, NUL-terminated media type, picture type,
// NUL-terminated description, picture bytes.
void parseApicFrame(const std::vector<std::uint8_t>& body, std::size_t offset, std::size_t end,
                    ArtworkPictureCandidate& best)
{
    if (offset >= end) {
        return;
    }

    const std::size_t terminatorSize = id3TerminatorSize(body[offset]);
    if (terminatorSize == 0) {
        return;
    }
    std::size_t cursor = skipId3String(body, offset + 1, end, 1);
    if (cursor >= end) {
        return;
    }

    const std::uint8_t pictureType = body[cursor];
    ++cursor;
    cursor = skipId3String(body, cursor, end, terminatorSize);
    if (cursor >= end) {
        return;
    }

    best.offer(tail(body, cursor, end), pictureType == PictureTypeFrontCover);
}

// PIC (ID3v2.2): encoding, three-character image format, picture type,
// NUL-terminated description, picture bytes.
void parsePicFrame(const std::vector<std::uint8_t>& body, std::size_t offset, std::size_t end,
                   ArtworkPictureCandidate& best)
{
    if (offset >= end || end - offset < 5) {
        return;
    }

    const std::size_t terminatorSize = id3TerminatorSize(body[offset]);
    if (terminatorSize == 0) {
        return;
    }

    const std::uint8_t pictureType = body[offset + 4];
    const std::size_t cursor = skipId3String(body, offset + 5, end, terminatorSize);
    if (cursor >= end) {
        return;
    }

    best.offer(tail(body, cursor, end), pictureType == PictureTypeFrontCover);
}

// Where the first frame starts: past the extended header when the tag has
// one. Nothing when that header is malformed.
std::optional<std::size_t> firstFrameOffset(const std::vector<std::uint8_t>& body, const Id3TagLayout& layout)
{
    if ((layout.flags & 0x40u) == 0) {
        return std::size_t{0};
    }
    if (body.size() < 4) {
        return std::nullopt;
    }
    if (layout.majorVersion >= 4) {
        // The v2.4 extended header size is syncsafe and counts itself.
        std::uint32_t extendedSize = 0;
        if (!tryReadSyncSafe32(body.data(), extendedSize) || extendedSize > body.size()) {
            return std::nullopt;
        }
        return std::size_t{extendedSize};
    }
    // The v2.3 extended header size leaves out its own four bytes.
    const std::uint32_t extendedSize = readBigEndian32(body.data());
    if (extendedSize > body.size() - 4) {
        return std::nullopt;
    }
    return 4u + std::size_t{extendedSize};
}

struct Id3FrameHeader {
    std::uint64_t size = 0;
    std::uint16_t flags = 0;
};

// The size and flags of the frame at `cursor`. `available` is the number of
// body bytes after the frame header.
Id3FrameHeader readFrameHeader(const std::vector<std::uint8_t>& body, std::size_t cursor, const Id3TagLayout& layout,
                               std::size_t idSize, std::size_t headerSize, std::size_t available)
{
    Id3FrameHeader header;
    if (layout.majorVersion == 2) {
        header.size = readBigEndian24(body.data() + cursor + 3);
        return header;
    }
    header.flags = static_cast<std::uint16_t>((body[cursor + 8] << 8) | body[cursor + 9]);
    if (layout.majorVersion == 3) {
        header.size = readBigEndian32(body.data() + cursor + 4);
        return header;
    }

    const auto plainSize = static_cast<std::uint64_t>(readBigEndian32(body.data() + cursor + 4));
    std::uint32_t syncSafeSize = 0;
    const bool syncSafe = tryReadSyncSafe32(body.data() + cursor + 4, syncSafeSize);
    header.size = syncSafe ? syncSafeSize : plainSize;

    // Several widely used taggers write ID3v2.4 frame sizes as plain
    // big-endian. Keep the syncsafe reading only when the frame it describes
    // is followed by padding, another frame, or the end of the tag.
    if (syncSafe && syncSafeSize != plainSize && plainSize <= available) {
        const std::size_t next = cursor + headerSize + syncSafeSize;
        const bool plausible = syncSafeSize <= available
            && (next + idSize > body.size() || body[next] == 0x00 || looksLikeId3FrameId(body.data() + next, idSize));
        if (!plausible) {
            header.size = plainSize;
        }
    }
    return header;
}

// Undoes the per-frame encodings of a picture frame. False when the frame
// must be skipped (compressed, encrypted or too short).
bool decodeFrameContent(std::vector<std::uint8_t>& content, const Id3TagLayout& layout, bool tagUnsynchronised,
                        std::uint16_t frameFlags)
{
    if (layout.majorVersion == 3) {
        // Compressed or encrypted frames need codecs this reader does not carry.
        if ((frameFlags & 0x0080u) != 0 || (frameFlags & 0x0040u) != 0) {
            return false;
        }
        if ((frameFlags & 0x0020u) != 0) {
            if (content.empty()) {
                return false;
            }
            content.erase(content.begin());
        }
    } else if (layout.majorVersion >= 4) {
        if ((frameFlags & 0x0008u) != 0 || (frameFlags & 0x0004u) != 0) {
            return false;
        }
        if (tagUnsynchronised || (frameFlags & 0x0002u) != 0) {
            removeUnsynchronisation(content);
        }
        if ((frameFlags & 0x0001u) != 0) {
            if (content.size() < 4) {
                return false;
            }
            content.erase(content.begin(), content.begin() + 4);
        }
    }
    return true;
}

} // namespace

bool tryReadId3TagLayout(ArtworkByteSource& source, std::uint64_t offset, Id3TagLayout& layout) noexcept
{
    std::array<std::uint8_t, 10> header{};
    if (!source.read(offset, header.data(), header.size())) {
        return false;
    }
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        return false;
    }
    if (header[3] < 2 || header[3] > 4 || header[4] == 0xFF) {
        return false;
    }

    std::uint32_t bodySize = 0;
    if (!tryReadSyncSafe32(header.data() + 6, bodySize)) {
        return false;
    }

    layout.majorVersion = header[3];
    layout.flags = header[5];
    layout.bodySize = bodySize;
    layout.totalSize = 10u + std::uint64_t{bodySize} + (((header[5] & 0x10u) != 0) ? 10u : 0u);
    return true;
}

void parseId3Tag(ArtworkByteSource& source, const Id3TagLayout& layout, ArtworkPictureCandidate& best)
{
    std::vector<std::uint8_t> body;
    if (!source.readInto(10, layout.bodySize, MaxId3TagBytes, body)) {
        return;
    }

    const bool tagUnsynchronised = (layout.flags & 0x80u) != 0;
    // ID3v2.3 and earlier unsynchronise the whole tag, so the frame sizes only
    // make sense after decoding it. ID3v2.4 moved this into the frames.
    if (tagUnsynchronised && layout.majorVersion < 4) {
        removeUnsynchronisation(body);
    }

    const std::optional<std::size_t> start = firstFrameOffset(body, layout);
    if (!start) {
        return;
    }
    std::size_t cursor = *start;
    const std::size_t idSize = layout.majorVersion == 2 ? 3 : 4;
    const std::size_t headerSize = layout.majorVersion == 2 ? 6 : 10;

    for (int frame = 0; frame < MaxId3Frames; ++frame) {
        if (cursor + headerSize > body.size()) {
            return;
        }
        // Padding: the rest of the tag is zeroes.
        if (body[cursor] == 0x00 || !looksLikeId3FrameId(body.data() + cursor, idSize)) {
            return;
        }

        const std::size_t available = body.size() - cursor - headerSize;
        const Id3FrameHeader header = readFrameHeader(body, cursor, layout, idSize, headerSize, available);
        if (header.size > available) {
            return;
        }

        const std::size_t frameStart = cursor;
        const std::size_t contentStart = frameStart + headerSize;
        const std::size_t contentEnd = contentStart + static_cast<std::size_t>(header.size);
        cursor = contentEnd;

        const bool isPictureFrame = layout.majorVersion == 2 ? std::memcmp(body.data() + frameStart, "PIC", 3) == 0
                                                             : std::memcmp(body.data() + frameStart, "APIC", 4) == 0;
        if (!isPictureFrame) {
            continue;
        }

        std::vector<std::uint8_t> content = tail(body, contentStart, contentEnd);
        if (!decodeFrameContent(content, layout, tagUnsynchronised, header.flags)) {
            continue;
        }

        if (layout.majorVersion == 2) {
            parsePicFrame(content, 0, content.size(), best);
        } else {
            parseApicFrame(content, 0, content.size(), best);
        }

        if (best.hasFrontCover()) {
            return;
        }
    }
}

} // namespace lmp::core::artwork
