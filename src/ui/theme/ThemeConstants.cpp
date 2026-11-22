#include "ui/theme/ThemeConstants.h"

#include <QVariant>

#include "core/model/ArtworkPlaceholder.h"
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
    : ThemeFullscreen(parent)
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

QVariantList ThemeConstants::shadowBubble()
{
    return shadowLayers({{0, 4, 14, 0, rgba(0, 0, 0, 0.22f)}});
}

QVariantList ThemeConstants::shadowMenu()
{
    return shadowLayers({{0, 8, 24, 0, rgba(0, 0, 0, 0.18f)}, {0, 2, 6, 0, rgba(0, 0, 0, 0.10f)}});
}

QVariantList ThemeConstants::shadowDialog()
{
    return shadowLayers({{0, 24, 60, 0, rgba(0, 0, 0, 0.28f)}});
}

QVariantList ThemeConstants::shadowToast()
{
    return shadowLayers({{0, 14, 34, 0, rgba(0, 0, 0, 0.22f)}});
}

QColor ThemeConstants::artLabel()
{
    return rgba(255, 255, 255, 0.88f);
}

QColor ThemeConstants::artLabelShadow()
{
    return rgba(0, 0, 0, 0.18f);
}

QColor ThemeConstants::artSheenTop()
{
    return rgba(255, 255, 255, 0.15f);
}

QColor ThemeConstants::artSheenBottom()
{
    return rgba(0, 0, 0, 0.10f);
}

QColor ThemeConstants::captionCloseHover()
{
    return QColor(0xc4, 0x2b, 0x1c);
}

QColor ThemeConstants::captionCloseGlyph()
{
    return QColor(Qt::white);
}

QColor ThemeConstants::brandLogoStart()
{
    return QColor(0x00, 0xAF, 0xCA);
}

QColor ThemeConstants::brandLogoEnd()
{
    return QColor(0x00, 0x9F, 0xBA);
}

QColor ThemeConstants::brandLogoBar()
{
    return QColor(Qt::white);
}

QColor ThemeConstants::idleDisc()
{
    return QColor(0x1A, 0x1A, 0x1C);
}

QColor ThemeConstants::idleDiscGroove()
{
    return QColor(0x26, 0x26, 0x2A);
}

QColor ThemeConstants::idleDiscHighlight()
{
    return rgba(255, 255, 255, 0.16f);
}

QColor ThemeConstants::idleDiscRing()
{
    return rgba(255, 255, 255, 0.06f);
}

QVariantList ThemeConstants::shadowIdleDisc()
{
    return shadowLayers({{0, 2, 6, 0, rgba(0, 0, 0, 0.28f)}});
}

QColor ThemeConstants::railShadow()
{
    return rgba(0, 0, 0, 0.18f);
}

QColor ThemeConstants::swatchRing()
{
    return rgba(0, 0, 0, 0.12f);
}

QColor ThemeConstants::artworkGradientStart(const QString& id) const
{
    const int index = core::ArtworkPlaceholder::gradientIndexForId(
        id, static_cast<int>(kArtworkGradientCount));
    return artworkGradients().at(static_cast<std::size_t>(index)).first;
}

QColor ThemeConstants::artworkGradientEnd(const QString& id) const
{
    const int index = core::ArtworkPlaceholder::gradientIndexForId(
        id, static_cast<int>(kArtworkGradientCount));
    return artworkGradients().at(static_cast<std::size_t>(index)).second;
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
