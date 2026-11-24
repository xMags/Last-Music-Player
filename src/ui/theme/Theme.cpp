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
    // The default ambient colour follows the accent.
    connect(this, &ThemeColors::changed, this, &Theme::ambientChanged);
}

QColor Theme::ambient() const
{
    return m_ambientOverride.isValid() ? m_ambientOverride : accentSeed();
}

void Theme::setAmbient(const QColor& color)
{
    if (color == m_ambientOverride) {
        return;
    }
    m_ambientOverride = color;
    emit ambientChanged();
}

void Theme::resetAmbient()
{
    setAmbient(QColor());
}

qreal Theme::ambientStrength() const
{
    return dark() ? 1.45 : 1.0;
}

QVariantList Theme::shadow() const
{
    return shadowLayers({{0, 1, 3, 0, black(dark() ? 0.32f : 0.06f)}});
}

QVariantList Theme::shadowCard() const
{
    return shadowLayers({{0, 2, 6, 0, black(dark() ? 0.36f : 0.07f)}});
}

QVariantList Theme::shadowCardHover() const
{
    // A hovered card's cover: the card shadow and a deeper one below
    // (styles.css 597-599).
    return shadowLayers({{0, 2, 6, 0, black(dark() ? 0.36f : 0.07f)}, {0, 14, 28, -12, black(0.38f)}});
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

QVariantList Theme::shadowWelcomeFoot() const
{
    return shadowLayers({{0, 18, 44, -18, black(0.34f)}});
}

QVariantList Theme::shadowCatalogHeroArt() const
{
    return shadowLayers({{0, 12, 32, 0, black(0.16f)}});
}

QVariantList Theme::shadowShelfArrow() const
{
    return shadowLayers({{0, 2, 12, 0, black(0.22f)}});
}

QVariantList Theme::shadowSearchTopArt() const
{
    return shadowLayers({{0, 10, 28, 0, black(0.35f)}});
}

QVariantList Theme::shadowSearchTopPlay() const
{
    return shadowLayers({{0, 8, 22, 0, black(0.40f)}});
}

QVariantList Theme::shadowCategoryCover() const
{
    return shadowLayers({{0, 6, 18, 0, black(0.28f)}});
}

QVariantList Theme::shadowHeroArt() const
{
    return shadowLayers({{0, 18, 40, 0, black(0.35f)}});
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

QColor Theme::queueClear() const
{
    return dark() ? QColor(0xFF, 0x00, 0x00) : QColor(0xE0, 0x00, 0x00);
}

QColor Theme::queueClearHover() const
{
    return dark() ? QColor(0xFF, 0x4D, 0x4D) : QColor(0xB8, 0x00, 0x00);
}

QColor Theme::scrim() const
{
    if (dark()) {
        return black(0.55f);
    }
    QColor color(10, 14, 22);
    color.setAlphaF(0.42f);
    return color;
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
