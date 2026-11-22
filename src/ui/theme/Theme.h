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

    // Colours of a press ripple drawn on a surface of the given colour: light
    // water on dark or coloured surfaces, a faint shadow and an accent ring on
    // light ones. A mostly transparent surface follows the theme.
    Q_INVOKABLE QColor rippleFillOn(const QColor& surface) const;
    Q_INVOKABLE QColor rippleRingOn(const QColor& surface) const;

private:
    [[nodiscard]] bool darkSurface(const QColor& surface) const;
};

} // namespace lmp::ui
