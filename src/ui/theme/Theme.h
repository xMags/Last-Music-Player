#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ui/theme/ThemeColors.h"

namespace lmp::ui {

// The design system for QML: every token colour (ThemeColors), size and
// fixed colour (ThemeConstants) and text style (ThemeTypography), plus the
// colours this class derives from them for shadows and controls.
class Theme : public ThemeColors {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Box shadows: lists of ShadowLayer, drawn in order.
    Q_PROPERTY(QVariantList shadow READ shadow NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowCard READ shadowCard NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowCardHover READ shadowCardHover NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowFlyout READ shadowFlyout NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowArt READ shadowArt NOTIFY changed FINAL)
    // Home's hero cover (.home-hero-art: 0 18px 40px rgba(0,0,0,.35)).
    Q_PROPERTY(QVariantList shadowHeroArt READ shadowHeroArt CONSTANT FINAL)
    // A Browse category tile's tilted cover (0 6px 18px rgba(0,0,0,.28)).
    Q_PROPERTY(QVariantList shadowCategoryCover READ shadowCategoryCover CONSTANT FINAL)
    // A search top card's Play (0 8px 22px rgba(0,0,0,.4)).
    Q_PROPERTY(QVariantList shadowSearchTopPlay READ shadowSearchTopPlay CONSTANT FINAL)
    // A search top card's cover (0 10px 28px rgba(0,0,0,.35)).
    Q_PROPERTY(QVariantList shadowSearchTopArt READ shadowSearchTopArt CONSTANT FINAL)
    // A shelf's scroll arrow (0 2px 12px rgba(0,0,0,.22)).
    Q_PROPERTY(QVariantList shadowShelfArrow READ shadowShelfArrow CONSTANT FINAL)
    // A catalogue hero's art (0 12px 32px rgba(0,0,0,.16)).
    Q_PROPERTY(QVariantList shadowCatalogHeroArt READ shadowCatalogHeroArt CONSTANT FINAL)
    // The welcome flow's floating footer (0 18px 44px -18px rgba(0,0,0,.34)).
    Q_PROPERTY(QVariantList shadowWelcomeFoot READ shadowWelcomeFoot CONSTANT FINAL)
    // The sign-in gate: the record, the sleeve and the hovered button.
    Q_PROPERTY(QVariantList shadowSignInRecord READ shadowSignInRecord CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowSignInSleeve READ shadowSignInSleeve CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowSignInButton READ shadowSignInButton CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowButton READ shadowButton NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowButtonHover READ shadowButtonHover NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowSliderThumb READ shadowSliderThumb NOTIFY changed FINAL)

    // Slider track behind the fill, and scroll bar thumbs.
    Q_PROPERTY(QColor sliderTrack READ sliderTrack NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrollThumb READ scrollThumb NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrollThumbHover READ scrollThumbHover NOTIFY changed FINAL)
    // Loading placeholders: the accent tinted into the surface. Covers and
    // secondary lines, title lines, and the line glyph on a cover.
    Q_PROPERTY(QColor skeleton READ skeleton NOTIFY changed FINAL)
    Q_PROPERTY(QColor skeletonStrong READ skeletonStrong NOTIFY changed FINAL)
    Q_PROPERTY(QColor skeletonGlyph READ skeletonGlyph NOTIFY changed FINAL)
    // The dimmed backdrop behind a dialog.
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed FINAL)
    // The Up Next "Clear" action, red because it is destructive.
    Q_PROPERTY(QColor queueClear READ queueClear NOTIFY changed FINAL)
    Q_PROPERTY(QColor queueClearHover READ queueClearHover NOTIFY changed FINAL)
    // The current cover's colour that tints the panels; the accent's seed
    // colour until playback sets one. Readers fade to a new value over
    // Motion.ambient.
    Q_PROPERTY(QColor ambient READ ambient WRITE setAmbient RESET resetAmbient NOTIFY ambientChanged FINAL)
    // How strongly the ambient colour shows: 1 light, 1.45 dark.
    Q_PROPERTY(qreal ambientStrength READ ambientStrength NOTIFY changed FINAL)

public:
    explicit Theme(QObject* parent = nullptr);

    [[nodiscard]] QVariantList shadow() const;
    [[nodiscard]] QVariantList shadowCard() const;
    [[nodiscard]] QVariantList shadowCardHover() const;
    [[nodiscard]] QVariantList shadowFlyout() const;
    [[nodiscard]] QVariantList shadowArt() const;
    [[nodiscard]] QVariantList shadowHeroArt() const;
    [[nodiscard]] QVariantList shadowCategoryCover() const;
    [[nodiscard]] QVariantList shadowSearchTopPlay() const;
    [[nodiscard]] QVariantList shadowSearchTopArt() const;
    [[nodiscard]] QVariantList shadowShelfArrow() const;
    [[nodiscard]] QVariantList shadowCatalogHeroArt() const;
    [[nodiscard]] QVariantList shadowWelcomeFoot() const;
    [[nodiscard]] QVariantList shadowSignInRecord() const;
    [[nodiscard]] QVariantList shadowSignInSleeve() const;
    [[nodiscard]] QVariantList shadowSignInButton() const;
    [[nodiscard]] QVariantList shadowButton() const;
    [[nodiscard]] QVariantList shadowButtonHover() const;
    [[nodiscard]] QVariantList shadowSliderThumb() const;
    [[nodiscard]] QColor sliderTrack() const;
    [[nodiscard]] QColor scrollThumb() const;
    [[nodiscard]] QColor scrollThumbHover() const;
    [[nodiscard]] QColor skeleton() const;
    [[nodiscard]] QColor skeletonStrong() const;
    [[nodiscard]] QColor skeletonGlyph() const;
    [[nodiscard]] QColor scrim() const;
    [[nodiscard]] QColor queueClear() const;
    [[nodiscard]] QColor queueClearHover() const;
    [[nodiscard]] QColor ambient() const;
    void setAmbient(const QColor& color);
    void resetAmbient();
    [[nodiscard]] qreal ambientStrength() const;

    // Colours of a press ripple drawn on a surface of the given colour: light
    // water on dark or coloured surfaces, a faint shadow and an accent ring on
    // light ones. A mostly transparent surface follows the theme.
    Q_INVOKABLE QColor rippleFillOn(const QColor& surface) const;
    Q_INVOKABLE QColor rippleRingOn(const QColor& surface) const;

signals:
    void ambientChanged();

private:
    // The colour the skeleton tokens mix into the surface.
    [[nodiscard]] QColor skeletonTint() const;
    [[nodiscard]] bool darkSurface(const QColor& surface) const;

    // Invalid while no song colour is set.
    QColor m_ambientOverride;
};

} // namespace lmp::ui
