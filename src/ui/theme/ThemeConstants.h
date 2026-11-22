#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "ui/theme/ThemeTypography.h"

namespace lmp::ui {

// The part of the theme that is the same in light and dark and for every
// accent: corner radii, fixed layout sizes and fixed colours. Theme derives
// from it (and it from ThemeTypography) so QML reads every token flat, as
// `Theme.radiusMd`.
class ThemeConstants : public ThemeTypography {
    Q_OBJECT

    // Corner scale.
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT FINAL)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT FINAL)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT FINAL)
    Q_PROPERTY(int radiusXl READ radiusXl CONSTANT FINAL)
    Q_PROPERTY(int radius2xl READ radius2xl CONSTANT FINAL)

    // Layout sizes. The wide values apply from wideWindowMinWidth upwards.
    Q_PROPERTY(int playerBarHeight READ playerBarHeight CONSTANT FINAL)
    Q_PROPERTY(int sidebarLeftWidth READ sidebarLeftWidth CONSTANT FINAL)
    Q_PROPERTY(int sidebarRightWidth READ sidebarRightWidth CONSTANT FINAL)
    Q_PROPERTY(int sidebarLeftWidthWide READ sidebarLeftWidthWide CONSTANT FINAL)
    Q_PROPERTY(int sidebarRightWidthWide READ sidebarRightWidthWide CONSTANT FINAL)
    Q_PROPERTY(int wideWindowMinWidth READ wideWindowMinWidth CONSTANT FINAL)
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT FINAL)

    // Each entry is a list of two colours: gradient start and end.
    Q_PROPERTY(QVariantList artworkGradients READ artworkGradientList CONSTANT FINAL)
    // The five accent seed colours, in Theme.Accent order, for the accent picker.
    Q_PROPERTY(QVariantList accentSeeds READ accentSeedList CONSTANT FINAL)

    // Colours that are the same in both themes.
    Q_PROPERTY(QColor rippleFill READ rippleFill CONSTANT FINAL)
    Q_PROPERTY(QColor rippleFillOnDark READ rippleFillOnDark CONSTANT FINAL)
    Q_PROPERTY(QColor rippleRingOnDark READ rippleRingOnDark CONSTANT FINAL)
    // Round buttons drawn over artwork, and their glyph colour.
    Q_PROPERTY(QColor overlayButton READ overlayButton CONSTANT FINAL)
    Q_PROPERTY(QColor overlayButtonHover READ overlayButtonHover CONSTANT FINAL)
    Q_PROPERTY(QColor onOverlay READ onOverlay CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowPlayButton READ shadowPlayButton CONSTANT FINAL)
    // The value bubble over the player's sliders.
    Q_PROPERTY(QVariantList shadowBubble READ shadowBubble CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowMenu READ shadowMenu CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowDialog READ shadowDialog CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowToast READ shadowToast CONSTANT FINAL)

    // Cover placeholders: the letter, its soft shadow, and the light-to-dark
    // sheen laid over the gradient.
    Q_PROPERTY(QColor artLabel READ artLabel CONSTANT FINAL)
    Q_PROPERTY(QColor artLabelShadow READ artLabelShadow CONSTANT FINAL)
    Q_PROPERTY(QColor artSheenTop READ artSheenTop CONSTANT FINAL)
    Q_PROPERTY(QColor artSheenBottom READ artSheenBottom CONSTANT FINAL)
    // The close caption button under the pointer, and its glyph.
    Q_PROPERTY(QColor captionCloseHover READ captionCloseHover CONSTANT FINAL)
    Q_PROPERTY(QColor captionCloseGlyph READ captionCloseGlyph CONSTANT FINAL)
    // The application logo: its gradient tile and white bars.
    Q_PROPERTY(QColor brandLogoStart READ brandLogoStart CONSTANT FINAL)
    Q_PROPERTY(QColor brandLogoEnd READ brandLogoEnd CONSTANT FINAL)
    Q_PROPERTY(QColor brandLogoBar READ brandLogoBar CONSTANT FINAL)
    // The shadow of the queue rail when it opens over the content.
    Q_PROPERTY(QColor railShadow READ railShadow CONSTANT FINAL)
    // The thin ring inside an accent colour swatch.
    Q_PROPERTY(QColor swatchRing READ swatchRing CONSTANT FINAL)

    Q_PROPERTY(qreal disabledOpacity READ disabledOpacity CONSTANT FINAL)
    Q_PROPERTY(qreal disabledSwitchOpacity READ disabledSwitchOpacity CONSTANT FINAL)
    Q_PROPERTY(int focusRingWidth READ focusRingWidth CONSTANT FINAL)

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

    // The two colours of the placeholder gradient for an item id.
    Q_INVOKABLE QColor artworkGradientStart(const QString& id) const;
    Q_INVOKABLE QColor artworkGradientEnd(const QString& id) const;

    [[nodiscard]] static QColor rippleFill();
    [[nodiscard]] static QColor rippleFillOnDark();
    [[nodiscard]] static QColor rippleRingOnDark();
    [[nodiscard]] static QColor overlayButton();
    [[nodiscard]] static QColor overlayButtonHover();
    [[nodiscard]] static QColor onOverlay();
    [[nodiscard]] static QVariantList shadowPlayButton();
    [[nodiscard]] static QColor artLabel();
    [[nodiscard]] static QColor artLabelShadow();
    [[nodiscard]] static QColor artSheenTop();
    [[nodiscard]] static QColor artSheenBottom();
    [[nodiscard]] static QColor swatchRing();
    [[nodiscard]] static QColor railShadow();
    [[nodiscard]] static QColor brandLogoStart();
    [[nodiscard]] static QColor brandLogoEnd();
    [[nodiscard]] static QColor brandLogoBar();
    [[nodiscard]] static QColor captionCloseHover();
    [[nodiscard]] static QColor captionCloseGlyph();
    [[nodiscard]] static QVariantList shadowBubble();
    [[nodiscard]] static QVariantList shadowMenu();
    [[nodiscard]] static QVariantList shadowDialog();
    [[nodiscard]] static QVariantList shadowToast();

    // Disabled buttons fade to this opacity.
    [[nodiscard]] qreal disabledOpacity() const { return 0.55; }
    // Switch rows fade a little further.
    [[nodiscard]] qreal disabledSwitchOpacity() const { return 0.5; }
    [[nodiscard]] int focusRingWidth() const { return 2; }

};

} // namespace lmp::ui
