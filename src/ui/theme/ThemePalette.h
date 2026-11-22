#pragma once

#include <QColor>

#include <array>
#include <utility>

namespace lmp::ui {

// The colours that differ between light and dark theme. Values are copied
// from the shared design tokens; the accent-dependent colours live in
// BrandAccent because they change with the user's accent choice.
struct ThemePalette {
    QColor onPrimary;
    QColor surface;
    QColor surfaceAlt;
    QColor canvas;
    QColor neutral;
    QColor grad1;
    QColor grad2;
    QColor stroke;
    QColor strokeStrong;
    QColor divider;
    QColor rowHover;
    QColor controlHover;
    QColor segmentTrack;
    QColor segmentSelected;
    QColor fg1;
    QColor fg2;
    QColor fg3;
    QColor fg4;
    QColor playBg;
    QColor playFg;
    QColor danger;
    // Placeholder tint behind artwork that has not loaded yet.
    QColor artGrad1;
    QColor artGrad2;

    [[nodiscard]] static ThemePalette light();
    [[nodiscard]] static ThemePalette dark();
};

// One of the five brand accents. The seed is shown in the picker and used for
// decorative fills; light and dark are the contrast-tuned primary colours.
struct BrandAccent {
    QColor seed;
    QColor light;
    QColor dark;
};

inline constexpr std::size_t kBrandAccentCount = 5;
inline constexpr std::size_t kArtworkGradientCount = 9;

// In the order teal, purple, coral, green, amber. Teal is the default.
[[nodiscard]] const std::array<BrandAccent, kBrandAccentCount>& brandAccents();

// Start and end colour of the gradients drawn when a cover is missing.
[[nodiscard]] const std::array<std::pair<QColor, QColor>, kArtworkGradientCount>&
artworkGradients();

} // namespace lmp::ui
