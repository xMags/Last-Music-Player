#include "ui/theme/ThemeConstants.h"

#include <QVariant>

#include "ui/theme/ShadowLayer.h"
#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

namespace {

QColor rgba(int red, int green, int blue, float alpha)
{
    QColor color(red, green, blue);
    color.setAlphaF(alpha);
    return color;
}

} // namespace

ThemeConstants::ThemeConstants(QObject* parent)
    : ThemeTypography(parent)
{
}

QColor ThemeConstants::rippleFill()
{
    return rgba(0, 0, 0, 0.12f);
}

QColor ThemeConstants::rippleFillOnDark()
{
    return rgba(255, 255, 255, 0.2f);
}

QColor ThemeConstants::rippleRingOnDark()
{
    return rgba(255, 255, 255, 0.55f);
}

QColor ThemeConstants::overlayButton()
{
    return rgba(20, 24, 32, 0.56f);
}

QColor ThemeConstants::overlayButtonHover()
{
    return rgba(20, 24, 32, 0.76f);
}

QColor ThemeConstants::onOverlay()
{
    return QColor(Qt::white);
}

QVariantList ThemeConstants::shadowPlayButton()
{
    return shadowLayers({{0, 2, 8, 0, rgba(0, 0, 0, 0.16f)}});
}

QVariantList ThemeConstants::artworkGradientList()
{
    QVariantList list;
    for (const auto& [start, end] : artworkGradients()) {
        list.append(QVariant::fromValue(QVariantList{start, end}));
    }
    return list;
}

QVariantList ThemeConstants::accentSeedList()
{
    QVariantList list;
    for (const BrandAccent& accent : brandAccents()) {
        list.append(accent.seed);
    }
    return list;
}

} // namespace lmp::ui
