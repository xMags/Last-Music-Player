#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <cstddef>
#include <optional>

namespace lmp::core {

// A cover picture exactly as it is stored inside an audio file. The bytes are
// never decoded or re-encoded here, so whatever resolution the tagger wrote is
// what reaches the display path.
struct EmbeddedArtwork {
    QByteArray bytes;
    // Lower-case media type such as "image/jpeg", taken from the payload's
    // signature (a payload with no known image signature is never returned).
    QString mimeType;
};

// Largest picture payload accepted from an untrusted media file. Real cover art
// is far below this (a lossless 4000x4000 PNG is around 20 MB), while the cap
// bounds the allocation a hand-crafted length field can ask for.
inline constexpr std::size_t MaxEmbeddedArtworkBytes = 24u * 1024u * 1024u;

// Reads the cover picture from an in-memory copy of an audio file. Understands
// ID3v2.2/2.3/2.4 (mp3, and ID3-prefixed flac), native FLAC PICTURE blocks and
// MP4/M4A `covr` atoms; the container is found from its signature, not from a
// file name. Nothing for any other container, for files without a picture and
// for malformed or truncated input. Never throws.
[[nodiscard]] std::optional<EmbeddedArtwork> tryReadEmbeddedArtwork(QByteArrayView fileBytes) noexcept;

// The same parse against a file on disk. Only the metadata regions are read,
// never the whole file. Blocking file I/O: call it off the UI thread.
[[nodiscard]] std::optional<EmbeddedArtwork> tryReadEmbeddedArtworkFromFile(const QString& filePath) noexcept;

// File extension, with the leading dot, for a picture media type. ".jpg" for
// unknown or empty types, since the image decoders used read the content
// rather than the name.
[[nodiscard]] QString artworkFileExtension(QStringView mimeType);

} // namespace lmp::core
