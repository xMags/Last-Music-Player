#include "ui/theme/ThemeFullscreen.h"

#include <QString>

#include "ui/theme/ShadowLayer.h"

namespace lmp::ui {

namespace {

constexpr int kRegular = 400;
constexpr int kSemiBold = 600;
constexpr int kBold = 700;

QColor white(float alpha)
{
    QColor color(Qt::white);
    color.setAlphaF(alpha);
    return color;
}

QColor rgba(int red, int green, int blue, float alpha)
{
    QColor color(red, green, blue);
    color.setAlphaF(alpha);
    return color;
}

} // namespace

ThemeFullscreen::ThemeFullscreen(QObject* parent)
    : ThemeTypography(parent)
{
    const QString family = fontText();
    // .fs-artist: 16px, line-height 1.35.
    m_artist = makeTextStyle({.family = family, .pixelSize = 16, .weight = kRegular, .lineHeight = 16 * 1.35});
    // .fs-meta: 12.5px.
    m_meta = makeTextStyle({.family = family, .pixelSize = 12.5, .weight = kRegular});
    // .fs-time: 12px, tabular figures.
    m_time = makeTextStyle({.family = family, .pixelSize = 12, .weight = kRegular, .tabularNumbers = true});
    // .fs-tab: 13.5px; the chosen one 600.
    m_tab = makeTextStyle({.family = family, .pixelSize = 13.5, .weight = kRegular});
    m_tabActive = makeTextStyle({.family = family, .pixelSize = 13.5, .weight = kSemiBold});
    // .fs-btn .repeat-one-badge: 9px, 700.
    m_repeatBadge = makeTextStyle({.family = family, .pixelSize = 9, .weight = kBold});
}

QColor ThemeFullscreen::fullscreenBackground()
{
    return QColor(0x0e, 0x10, 0x16);
}

QColor ThemeFullscreen::fullscreenText()
{
    return QColor(Qt::white);
}

QColor ThemeFullscreen::fullscreenSecondaryText()
{
    return white(0.78f);
}

QColor ThemeFullscreen::fullscreenTertiaryText()
{
    return white(0.56f);
}

QColor ThemeFullscreen::fullscreenArtistText()
{
    return white(0.75f);
}

QColor ThemeFullscreen::fullscreenMetaText()
{
    return white(0.55f);
}

QColor ThemeFullscreen::fullscreenTimeText()
{
    return white(0.6f);
}

QColor ThemeFullscreen::fullscreenEmptyText()
{
    return white(0.6f);
}

QColor ThemeFullscreen::fullscreenButtonGlyph()
{
    return white(0.85f);
}

QColor ThemeFullscreen::fullscreenButtonHover()
{
    return white(0.12f);
}

QColor ThemeFullscreen::fullscreenPlayFill()
{
    return QColor(Qt::white);
}

QColor ThemeFullscreen::fullscreenPlayGlyph()
{
    return QColor(0x10, 0x12, 0x16);
}

QColor ThemeFullscreen::fullscreenSeekFill()
{
    return QColor(Qt::white);
}

QColor ThemeFullscreen::fullscreenSeekTrack()
{
    return white(0.22f);
}

QColor ThemeFullscreen::fullscreenTabsFill()
{
    return white(0.07f);
}

QColor ThemeFullscreen::fullscreenTabsBorder()
{
    return white(0.1f);
}

QColor ThemeFullscreen::fullscreenTabText()
{
    return white(0.7f);
}

QColor ThemeFullscreen::fullscreenTabActiveFill()
{
    return white(0.16f);
}

QColor ThemeFullscreen::fullscreenPanelFill()
{
    return rgba(8, 10, 16, 0.34f);
}

QColor ThemeFullscreen::fullscreenPanelBorder()
{
    return white(0.08f);
}

QColor ThemeFullscreen::fullscreenBackdropEnd()
{
    return QColor(0x1a, 0x23, 0x40);
}

QColor ThemeFullscreen::fullscreenBackdropDim()
{
    return rgba(0, 0, 0, 0.58f);
}

QColor ThemeFullscreen::fullscreenCaptionGlyph()
{
    return white(0.9f);
}

QVariantList ThemeFullscreen::shadowFullscreenArt()
{
    return shadowLayers({{0, 30, 80, 0, rgba(0, 0, 0, 0.55f)}});
}

QVariantList ThemeFullscreen::shadowFullscreenPlay()
{
    return shadowLayers({{0, 10, 28, 0, rgba(0, 0, 0, 0.35f)}});
}

} // namespace lmp::ui
