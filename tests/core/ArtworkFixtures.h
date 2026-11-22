#pragma once

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Builders for the embedded artwork tests. Every media file those tests use is
// assembled here from bytes, so no media file is stored anywhere.
namespace artwork_fixtures {

using Bytes = std::vector<std::uint8_t>;

inline QByteArray toByteArray(const Bytes& bytes)
{
    QByteArray result(static_cast<qsizetype>(bytes.size()), Qt::Uninitialized);
    if (!bytes.empty()) {
        std::memcpy(result.data(), bytes.data(), bytes.size());
    }
    return result;
}

inline void append(Bytes& target, const Bytes& source)
{
    target.insert(target.end(), source.begin(), source.end());
}

inline void appendAscii(Bytes& target, const char* text)
{
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        target.push_back(static_cast<std::uint8_t>(*cursor));
    }
}

inline void appendBigEndian32(Bytes& target, std::uint32_t value)
{
    target.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    target.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    target.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    target.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

inline void appendBigEndian24(Bytes& target, std::uint32_t value)
{
    target.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
    target.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    target.push_back(static_cast<std::uint8_t>(value & 0xFFu));
}

inline void appendSyncSafe32(Bytes& target, std::uint32_t value)
{
    target.push_back(static_cast<std::uint8_t>((value >> 21) & 0x7Fu));
    target.push_back(static_cast<std::uint8_t>((value >> 14) & 0x7Fu));
    target.push_back(static_cast<std::uint8_t>((value >> 7) & 0x7Fu));
    target.push_back(static_cast<std::uint8_t>(value & 0x7Fu));
}

// A minimal JPEG with a correct signature. `marker` tells payloads apart so a
// test can prove which of several pictures was chosen.
inline Bytes makeJpeg(std::uint8_t marker, std::size_t size = 64)
{
    Bytes bytes{0xFF, 0xD8, 0xFF, 0xE0};
    bytes.resize(size, marker);
    return bytes;
}

inline Bytes makePng()
{
    Bytes bytes{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    bytes.resize(48, 0x11);
    return bytes;
}

// APIC payload: encoding, media type, picture type, description, picture.
inline Bytes makeApicContent(const char* mimeType, std::uint8_t pictureType, const Bytes& picture,
                             std::uint8_t encoding = 0, const char* description = "cover")
{
    Bytes content{encoding};
    appendAscii(content, mimeType);
    content.push_back(0x00);
    content.push_back(pictureType);
    appendAscii(content, description);
    content.push_back(0x00);
    if (encoding == 1 || encoding == 2) {
        content.push_back(0x00);
    }
    append(content, picture);
    return content;
}

// PIC payload (ID3v2.2): encoding, three-letter format, picture type,
// description, picture.
inline Bytes makePicContent(const char* format, std::uint8_t pictureType, const Bytes& picture)
{
    Bytes content{0x00};
    appendAscii(content, format);
    content.push_back(pictureType);
    appendAscii(content, "cover");
    content.push_back(0x00);
    append(content, picture);
    return content;
}

enum class FrameSizeEncoding {
    SyncSafe,
    BigEndian,
};

inline Bytes makeFrame(const char* id, const Bytes& content, std::uint8_t majorVersion,
                       FrameSizeEncoding sizeEncoding = FrameSizeEncoding::SyncSafe, std::uint16_t frameFlags = 0)
{
    Bytes frame;
    appendAscii(frame, id);
    if (majorVersion == 2) {
        appendBigEndian24(frame, static_cast<std::uint32_t>(content.size()));
    } else {
        if (majorVersion >= 4 && sizeEncoding == FrameSizeEncoding::SyncSafe) {
            appendSyncSafe32(frame, static_cast<std::uint32_t>(content.size()));
        } else {
            appendBigEndian32(frame, static_cast<std::uint32_t>(content.size()));
        }
        frame.push_back(static_cast<std::uint8_t>((frameFlags >> 8) & 0xFFu));
        frame.push_back(static_cast<std::uint8_t>(frameFlags & 0xFFu));
    }
    append(frame, content);
    return frame;
}

inline Bytes makeId3File(std::uint8_t majorVersion, const Bytes& frames, std::uint8_t tagFlags = 0,
                         const Bytes& trailer = {})
{
    Bytes file;
    appendAscii(file, "ID3");
    file.push_back(majorVersion);
    file.push_back(0x00);
    file.push_back(tagFlags);
    appendSyncSafe32(file, static_cast<std::uint32_t>(frames.size()));
    append(file, frames);
    append(file, trailer);
    return file;
}

inline Bytes makeFlacPictureBlock(std::uint32_t pictureType, const char* mimeType, const Bytes& picture)
{
    Bytes block;
    appendBigEndian32(block, pictureType);
    appendBigEndian32(block, static_cast<std::uint32_t>(std::string{mimeType}.size()));
    appendAscii(block, mimeType);
    appendBigEndian32(block, 0); // empty description
    appendBigEndian32(block, 500);
    appendBigEndian32(block, 500);
    appendBigEndian32(block, 24);
    appendBigEndian32(block, 0);
    appendBigEndian32(block, static_cast<std::uint32_t>(picture.size()));
    append(block, picture);
    return block;
}

inline void appendFlacBlock(Bytes& file, std::uint8_t blockType, bool isLast, const Bytes& block)
{
    file.push_back(static_cast<std::uint8_t>(blockType | (isLast ? 0x80u : 0x00u)));
    appendBigEndian24(file, static_cast<std::uint32_t>(block.size()));
    append(file, block);
}

inline Bytes makeAtom(const char* type, const Bytes& payload)
{
    Bytes atom;
    appendBigEndian32(atom, static_cast<std::uint32_t>(payload.size() + 8));
    appendAscii(atom, type);
    append(atom, payload);
    return atom;
}

inline Bytes makeMp4File(const Bytes& picture, std::uint32_t dataTypeIndicator, bool metaCarriesVersionFlags)
{
    Bytes dataPayload;
    appendBigEndian32(dataPayload, dataTypeIndicator);
    appendBigEndian32(dataPayload, 0); // locale
    append(dataPayload, picture);

    const Bytes covr = makeAtom("covr", makeAtom("data", dataPayload));
    const Bytes ilst = makeAtom("ilst", covr);

    Bytes metaPayload;
    if (metaCarriesVersionFlags) {
        appendBigEndian32(metaPayload, 0);
    }
    append(metaPayload, ilst);

    const Bytes moov = makeAtom("moov", makeAtom("udta", makeAtom("meta", metaPayload)));

    Bytes ftypPayload;
    appendAscii(ftypPayload, "M4A ");
    appendBigEndian32(ftypPayload, 0);

    Bytes file = makeAtom("ftyp", ftypPayload);
    append(file, moov);
    return file;
}

// Inserting 0x00 after every 0xFF is a superset of the ID3 rule and decodes the
// same, so the fixtures do not depend on which bytes really needed escaping.
inline Bytes unsynchronise(const Bytes& bytes)
{
    Bytes encoded;
    for (const std::uint8_t byte : bytes) {
        encoded.push_back(byte);
        if (byte == 0xFF) {
            encoded.push_back(0x00);
        }
    }
    return encoded;
}

} // namespace artwork_fixtures
