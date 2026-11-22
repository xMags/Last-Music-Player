#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace lmp::core {

// An 8-bit sRGB colour.
struct AccentRgb {
    int red = 0;
    int green = 0;
    int blue = 0;

    friend bool operator==(const AccentRgb&, const AccentRgb&) = default;
};

// The colour the panels take from the current cover (the ambient tint), as
// the design computes it: the cover is drawn at SampleSize x SampleSize, the
// most vivid colour family wins, and a fixed brightness of it is used. Pure
// arithmetic on pixels; decoding and scaling the picture is the caller's
// work, on a worker thread.
struct ArtworkAccent {
    // The cover is sampled at this many pixels on each side.
    static constexpr int SampleSize = 32;
    // Brightness (HSV value) of the ambient tint made from an accent.
    static constexpr double AmbientValue = 0.78;
    // The accent while nothing plays, and when nothing better is known.
    static constexpr AccentRgb IdleAccent{0, 151, 178};

    // The most vivid colour of a picture given as non-premultiplied RGBA
    // bytes, four per pixel (any whole number of pixels). Pixels that are
    // mostly transparent, nearly black, nearly white or nearly grey are
    // skipped; the rest are grouped by their top three bits per channel and
    // each group is scored by opacity, saturation and closeness to a middle
    // brightness. The best group's average colour wins (the first group seen
    // wins a tie). Nothing when no pixel counts.
    [[nodiscard]] static std::optional<AccentRgb> vibrantColour(std::span<const std::uint8_t> rgba);

    // The accent's hue at brightness `value` (0 to 1), with its saturation
    // held between 0.45 and 0.9.
    [[nodiscard]] static AccentRgb glowTone(AccentRgb accent, double value);

    // glowTone(accent, AmbientValue).
    [[nodiscard]] static AccentRgb ambientFor(AccentRgb accent);
};

} // namespace lmp::core
