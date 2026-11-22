#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

namespace {

QColor hex(const char* value)
{
    return QColor::fromString(QLatin1StringView(value));
}

// CSS rgba() with 0 to 255 channels and a 0 to 1 alpha.
QColor rgba(int red, int green, int blue, float alpha)
{
    QColor color(red, green, blue);
    color.setAlphaF(alpha);
    return color;
}

} // namespace

ThemePalette ThemePalette::light()
{
    return ThemePalette{
        .onPrimary = hex("#FFFFFF"),
        .surface = hex("#FFFFFF"),
        .surfaceAlt = hex("#FBFBFD"),
        .canvas = hex("#F3F3F3"),
        .neutral = hex("#F3F4F6"),
        .grad1 = hex("#F0FCFF"),
        .grad2 = hex("#F3F0FE"),
        .stroke = rgba(0, 0, 0, 0.06f),
        .strokeStrong = rgba(0, 0, 0, 0.10f),
        .divider = rgba(0, 0, 0, 0.06f),
        .rowHover = rgba(0, 0, 0, 0.024f),
        .controlHover = rgba(0, 0, 0, 0.04f),
        .segmentTrack = hex("#F3F4F6"),
        .segmentSelected = hex("#FFFFFF"),
        .fg1 = hex("#1A1A1A"),
        .fg2 = hex("#5C5C5C"),
        .fg3 = hex("#8A8A8A"),
        .fg4 = hex("#B4B4B4"),
        .playBg = hex("#1A202C"),
        .playFg = hex("#FFFFFF"),
        .danger = hex("#D83B3B"),
        .artGrad1 = hex("#F3EDFC"),
        .artGrad2 = hex("#FCF4FE"),
    };
}

ThemePalette ThemePalette::dark()
{
    return ThemePalette{
        .onPrimary = hex("#101415"),
        .surface = hex("#2B2B2B"),
        .surfaceAlt = hex("#272727"),
        .canvas = hex("#202020"),
        .neutral = hex("#3A3A3A"),
        .grad1 = hex("#1D2329"),
        .grad2 = hex("#21202B"),
        .stroke = rgba(255, 255, 255, 0.07f),
        .strokeStrong = rgba(255, 255, 255, 0.14f),
        .divider = rgba(255, 255, 255, 0.08f),
        .rowHover = rgba(255, 255, 255, 0.06f),
        .controlHover = rgba(255, 255, 255, 0.10f),
        .segmentTrack = rgba(255, 255, 255, 0.06f),
        .segmentSelected = rgba(255, 255, 255, 0.12f),
        .fg1 = hex("#FFFFFF"),
        .fg2 = hex("#C8C8C8"),
        .fg3 = hex("#9A9A9A"),
        .fg4 = hex("#6E6E6E"),
        .playBg = hex("#EDEDED"),
        .playFg = hex("#1A1A1A"),
        .danger = hex("#FF6B6B"),
        .artGrad1 = hex("#2a2233"),
        .artGrad2 = hex("#1f1d2a"),
    };
}

const std::array<BrandAccent, kBrandAccentCount>& brandAccents()
{
    static const std::array<BrandAccent, kBrandAccentCount> accents{{
        {hex("#0097B2"), hex("#00687A"), hex("#54D6EC")},
        {hex("#6A5CD6"), hex("#5044B2"), hex("#C8BFFF")},
        {hex("#E0574A"), hex("#A63B32"), hex("#FFB4AA")},
        {hex("#1F9D6B"), hex("#0D7049"), hex("#77DDAA")},
        {hex("#D88A2E"), hex("#8A5300"), hex("#FFC173")},
    }};
    return accents;
}

const std::array<std::pair<QColor, QColor>, kArtworkGradientCount>& artworkGradients()
{
    static const std::array<std::pair<QColor, QColor>, kArtworkGradientCount> gradients{{
        {hex("#8B7BE8"), hex("#6A5CD6")},
        {hex("#00B8D4"), hex("#0086A8")},
        {hex("#F0806A"), hex("#D8543E")},
        {hex("#3468D8"), hex("#2450B8")},
        {hex("#1F9D6B"), hex("#178A5C")},
        {hex("#C65FB0"), hex("#9C3F8C")},
        {hex("#D88A2E"), hex("#B0691A")},
        {hex("#5F7AC6"), hex("#3F5AA6")},
        {hex("#00A0A8"), hex("#007A86")},
    }};
    return gradients;
}

} // namespace lmp::ui
