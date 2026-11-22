#include "core/playback/ArtworkAccent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lmp::core {

namespace {

// Three bits per channel.
constexpr std::size_t kBucketCount = 512;

struct Bucket {
    double score = 0.0;
    int count = 0;
    std::uint64_t red = 0;
    std::uint64_t green = 0;
    std::uint64_t blue = 0;
};

// Rounds half up, as the design's arithmetic does (for values of either sign).
[[nodiscard]] int roundHalfUp(double value)
{
    return static_cast<int>(std::floor(value + 0.5));
}

} // namespace

std::optional<AccentRgb> ArtworkAccent::vibrantColour(std::span<const std::uint8_t> rgba)
{
    std::array<Bucket, kBucketCount> buckets{};
    // Keys in the order their first pixel was seen, so a tie goes to the
    // earliest group.
    std::vector<std::size_t> order;
    order.reserve(kBucketCount);

    const std::size_t pixelCount = rgba.size() / 4;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t index = pixel * 4;
        const double alpha = rgba[index + 3] / 255.0;
        if (alpha < 0.5) {
            continue;
        }
        const int red = rgba[index];
        const int green = rgba[index + 1];
        const int blue = rgba[index + 2];
        const double high = std::max({red, green, blue}) / 255.0;
        const double low = std::min({red, green, blue}) / 255.0;
        const double saturation = high > 0 ? (high - low) / high : 0.0;
        if (high < 0.12 || high > 0.97 || saturation < 0.18) {
            continue;
        }
        const auto key = static_cast<std::size_t>(((red >> 5) << 6) | ((green >> 5) << 3) | (blue >> 5));
        Bucket& bucket = buckets[key];
        if (bucket.count == 0) {
            order.push_back(key);
        }
        bucket.score += alpha * (0.35 + saturation) * (1 - std::abs(high - 0.62) * 0.45);
        bucket.count += 1;
        bucket.red += static_cast<std::uint64_t>(red);
        bucket.green += static_cast<std::uint64_t>(green);
        bucket.blue += static_cast<std::uint64_t>(blue);
    }

    const Bucket* winner = nullptr;
    for (const std::size_t key : order) {
        if (winner == nullptr || buckets[key].score > winner->score) {
            winner = &buckets[key];
        }
    }
    if (winner == nullptr) {
        return std::nullopt;
    }
    const auto count = static_cast<double>(winner->count);
    return AccentRgb{
        roundHalfUp(static_cast<double>(winner->red) / count),
        roundHalfUp(static_cast<double>(winner->green) / count),
        roundHalfUp(static_cast<double>(winner->blue) / count),
    };
}

AccentRgb ArtworkAccent::glowTone(AccentRgb accent, double value)
{
    const double red = accent.red / 255.0;
    const double green = accent.green / 255.0;
    const double blue = accent.blue / 255.0;
    const double high = std::max({red, green, blue});
    const double low = std::min({red, green, blue});
    const double delta = high - low;
    double hue = 0.0;
    if (delta != 0.0) {
        if (high == red) {
            hue = std::fmod((green - blue) / delta, 6.0);
        } else if (high == green) {
            hue = (blue - red) / delta + 2;
        } else {
            hue = (red - green) / delta + 4;
        }
        hue = std::fmod(hue * 60 + 360, 360.0);
    }
    const double saturation = std::max(0.45, std::min(0.9, high > 0 ? delta / high : 0.0));
    const double chroma = value * saturation;
    const double middle = chroma * (1 - std::abs(std::fmod(hue / 60, 2.0) - 1));
    const double offset = value - chroma;
    const int sector = static_cast<int>(std::floor(hue / 60)) % 6;

    std::array<double, 3> channels{};
    switch (sector) {
    case 0: channels = {chroma, middle, 0.0}; break;
    case 1: channels = {middle, chroma, 0.0}; break;
    case 2: channels = {0.0, chroma, middle}; break;
    case 3: channels = {0.0, middle, chroma}; break;
    case 4: channels = {middle, 0.0, chroma}; break;
    default: channels = {chroma, 0.0, middle}; break;
    }
    return AccentRgb{
        roundHalfUp((channels[0] + offset) * 255),
        roundHalfUp((channels[1] + offset) * 255),
        roundHalfUp((channels[2] + offset) * 255),
    };
}

AccentRgb ArtworkAccent::ambientFor(AccentRgb accent)
{
    return glowTone(accent, AmbientValue);
}

} // namespace lmp::core
