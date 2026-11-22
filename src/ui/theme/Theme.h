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
    Q_PROPERTY(QVariantList shadowFlyout READ shadowFlyout NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowArt READ shadowArt NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowButton READ shadowButton NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowButtonHover READ shadowButtonHover NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList shadowSliderThumb READ shadowSliderThumb NOTIFY changed FINAL)

    // Slider track behind the fill, and scroll bar thumbs.
    Q_PROPERTY(QColor sliderTrack READ sliderTrack NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrollThumb READ scrollThumb NOTIFY changed FINAL)
    Q_PROPERTY(QColor scrollThumbHover READ scrollThumbHover NOTIFY changed FINAL)
    // The dimmed backdrop behind a dialog.
    Q_PROPERTY(QColor scrim READ scrim NOTIFY changed FINAL)
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
    [[nodiscard]] QVariantList shadowFlyout() const;
    [[nodiscard]] QVariantList shadowArt() const;
    [[nodiscard]] QVariantList shadowButton() const;
    [[nodiscard]] QVariantList shadowButtonHover() const;
    [[nodiscard]] QVariantList shadowSliderThumb() const;
    [[nodiscard]] QColor sliderTrack() const;
    [[nodiscard]] QColor scrollThumb() const;
    [[nodiscard]] QColor scrollThumbHover() const;
    [[nodiscard]] QColor scrim() const;
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
    [[nodiscard]] bool darkSurface(const QColor& surface) const;

    // Invalid while no song colour is set.
    QColor m_ambientOverride;
};

} // namespace lmp::ui
