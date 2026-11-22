#include "ui/theme/Theme.h"

#include "ui/theme/ColorMix.h"
#include "ui/theme/ShadowLayer.h"

namespace lmp::ui {

namespace {

QColor black(float alpha)
{
    QColor color(Qt::black);
    color.setAlphaF(alpha);
    return color;
}

QColor white(float alpha)
{
    QColor color(Qt::white);
    color.setAlphaF(alpha);
    return color;
}

} // namespace

Theme::Theme(QObject* parent)
    : ThemeColors(parent)
{
}

QVariantList Theme::shadow() const
{
    return shadowLayers({{0, 1, 3, 0, black(dark() ? 0.32f : 0.06f)}});
}

QVariantList Theme::shadowCard() const
{
    return shadowLayers({{0, 2, 6, 0, black(dark() ? 0.36f : 0.07f)}});
}

// The same in both themes.
QVariantList Theme::shadowFlyout() const
{
    return shadowLayers({{0, 8, 16, 0, black(0.10f)}, {0, 0, 1, 0, black(0.20f)}});
}

QVariantList Theme::shadowArt() const
{
    if (dark()) {
        return shadowLayers({{0, 2, 4, 0, black(0.40f)},
                             {0, 16, 40, 0, black(0.50f)},
                             {0, 28, 60, 0, black(0.35f)}});
    }
    return shadowLayers({{0, 2, 4, 0, black(0.06f)},
                         {0, 12, 28, 0, black(0.14f)},
                         {0, 24, 48, 0, black(0.10f)}});
}

// The accent button glow follows the accent's seed colour.
QVariantList Theme::shadowButton() const
{
    return shadowLayers({{0, 1, 2, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.24)},
                         {0, 8, 22, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.34)}});
}

QVariantList Theme::shadowButtonHover() const
{
    return shadowLayers({{0, 2, 4, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.28)},
                         {0, 14, 32, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.42)}});
}

// A 1px ring in the strong stroke colour plus a small drop shadow.
QVariantList Theme::shadowSliderThumb() const
{
    return shadowLayers({{0, 0, 0, 1, strokeStrong()}, {0, 1, 2, 0, black(0.14f)}});
}

QColor Theme::sliderTrack() const
{
    return dark() ? white(0.18f) : black(0.10f);
}

QColor Theme::scrollThumb() const
{
    return dark() ? white(0.15f) : black(0.10f);
}

// The dark theme rule comes later in the design and keeps its colour on hover.
QColor Theme::scrollThumbHover() const
{
    return dark() ? scrollThumb() : black(0.20f);
}

QColor Theme::rippleFillOn(const QColor& surface) const
{
    return darkSurface(surface) ? rippleFillOnDark() : rippleFill();
}

QColor Theme::rippleRingOn(const QColor& surface) const
{
    return darkSurface(surface) ? rippleRingOnDark()
                                : lmp::ui::mixSrgb(primary(), Qt::transparent, 0.75);
}

// Relative luminance on the 0 to 255 scale, as the design computes it.
bool Theme::darkSurface(const QColor& surface) const
{
    if (!surface.isValid() || surface.alphaF() < 0.5f) {
        return dark();
    }
    const QColor rgb = surface.toRgb();
    const double luminance = 0.2126 * rgb.red() + 0.7152 * rgb.green() + 0.0722 * rgb.blue();
    return luminance < 140;
}

} // namespace lmp::ui
