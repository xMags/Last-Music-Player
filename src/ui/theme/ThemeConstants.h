#pragma once

#include <QObject>
#include <QVariantList>

namespace lmp::ui {

// The part of the theme that is the same in light and dark and for every
// accent: corner radii, fixed layout sizes and the fixed colour lists. Theme
// derives from it so QML reads every token flat, as `Theme.radiusMd`.
class ThemeConstants : public QObject {
    Q_OBJECT

    // Corner scale.
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(int radiusXl READ radiusXl CONSTANT)
    Q_PROPERTY(int radius2xl READ radius2xl CONSTANT)

    // Layout sizes. The wide values apply from wideWindowMinWidth upwards.
    Q_PROPERTY(int playerBarHeight READ playerBarHeight CONSTANT)
    Q_PROPERTY(int sidebarLeftWidth READ sidebarLeftWidth CONSTANT)
    Q_PROPERTY(int sidebarRightWidth READ sidebarRightWidth CONSTANT)
    Q_PROPERTY(int sidebarLeftWidthWide READ sidebarLeftWidthWide CONSTANT)
    Q_PROPERTY(int sidebarRightWidthWide READ sidebarRightWidthWide CONSTANT)
    Q_PROPERTY(int wideWindowMinWidth READ wideWindowMinWidth CONSTANT)
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT)

    // Each entry is a list of two colours: gradient start and end.
    Q_PROPERTY(QVariantList artworkGradients READ artworkGradientList CONSTANT)
    // The five accent seed colours, in Theme.Accent order, for the accent picker.
    Q_PROPERTY(QVariantList accentSeeds READ accentSeedList CONSTANT)

public:
    explicit ThemeConstants(QObject* parent = nullptr);

    [[nodiscard]] int radiusSm() const { return 6; }
    [[nodiscard]] int radiusMd() const { return 8; }
    [[nodiscard]] int radiusLg() const { return 12; }
    [[nodiscard]] int radiusXl() const { return 14; }
    [[nodiscard]] int radius2xl() const { return 16; }

    [[nodiscard]] int playerBarHeight() const { return 90; }
    [[nodiscard]] int sidebarLeftWidth() const { return 240; }
    [[nodiscard]] int sidebarRightWidth() const { return 300; }
    [[nodiscard]] int sidebarLeftWidthWide() const { return 300; }
    [[nodiscard]] int sidebarRightWidthWide() const { return 380; }
    [[nodiscard]] int wideWindowMinWidth() const { return 1500; }
    [[nodiscard]] int titleBarHeight() const { return 32; }

    [[nodiscard]] static QVariantList artworkGradientList();
    [[nodiscard]] static QVariantList accentSeedList();
};

} // namespace lmp::ui
