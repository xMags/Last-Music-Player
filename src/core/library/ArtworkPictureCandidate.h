#pragma once

#include "core/library/EmbeddedArtwork.h"

#include <QString>

#include <cstdint>
#include <vector>

namespace lmp::core::artwork {

// ID3v2 APIC and FLAC picture type of a front cover. A front cover wins; any
// other picture is only used when the file has no front cover.
inline constexpr std::uint32_t PictureTypeFrontCover = 0x03;

// The best picture found so far in one file, and whether it is a front cover,
// so a later front cover can replace an earlier "other" picture.
struct ArtworkPictureCandidate {
    EmbeddedArtwork artwork;
    bool isFrontCover = false;
    bool present = false;

    [[nodiscard]] bool hasFrontCover() const noexcept { return present && isFrontCover; }

    // Takes `bytes` when they are a non-empty image within the size limit and
    // either nothing is held yet or they are a front cover replacing a
    // picture that is not one.
    void offer(const std::vector<std::uint8_t>& bytes, bool frontCover);
};

// Media type from the payload's leading bytes; empty when it is not an image
// the platform can read. This also rejects payloads that are a link.
[[nodiscard]] QString sniffImageMimeType(const std::vector<std::uint8_t>& bytes);

} // namespace lmp::core::artwork
