#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    // A cover picture exactly as it is stored inside an audio file. The bytes are
    // never decoded or re-encoded here, so whatever resolution the tagger wrote
    // is what reaches the display path.
    struct EmbeddedArtwork
    {
        std::vector<std::uint8_t> Bytes;

        // Lowercase media type, for example L"image/jpeg". Empty when the
        // container named no type and the payload matched no known signature.
        std::wstring MimeType;
    };

    // Largest picture payload accepted from an untrusted media file. Real cover
    // art is far below this (a lossless 4000x4000 PNG lands around 20 MB), while
    // the cap bounds the allocation an attacker-controlled length field can ask
    // for when a hand-crafted file claims a multi-gigabyte picture.
    inline constexpr std::size_t kMaxEmbeddedArtworkBytes = 24u * 1024u * 1024u;

    // Read the cover picture out of a local audio file's metadata. Understands
    // ID3v2.2/2.3/2.4 (mp3, and ID3-prefixed flac), native FLAC PICTURE blocks,
    // and MP4/M4A `covr` atoms. Returns std::nullopt for any other container,
    // for files that carry no picture, and for malformed or truncated input.
    // Only the metadata regions are read, never the whole file. Never throws.
    std::optional<EmbeddedArtwork> TryReadEmbeddedArtwork(
        std::filesystem::path const& path) noexcept;

    // Same parse against an in-memory copy of the file. The container is probed
    // from its signature rather than from a file extension.
    std::optional<EmbeddedArtwork> TryReadEmbeddedArtwork(
        std::uint8_t const* fileBytes,
        std::size_t byteCount) noexcept;

    // Conventional file extension, including the leading dot, for a picture
    // media type. Falls back to L".jpg" for unknown or empty types because every
    // image decoder this app hands files to sniffs content rather than names.
    std::wstring ArtworkFileExtension(std::wstring const& mimeType);
}
