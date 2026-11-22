#include "ui/theme/TextStyles.h"

#include <QStringList>

#include "ui/theme/FontLoader.h"

namespace lmp::ui {

namespace {

// Weights use the CSS 100 to 1000 scale, which QFont shares.
constexpr int kRegular = 400;
constexpr int kSemiBold = 600;
constexpr int kBold = 700;
constexpr int kGreeting = 750;

struct StyleSpec {
    const QString& family;
    qreal pixelSize;
    int weight;
    qreal lineHeight = 0;
    // CSS letter-spacing in em; converted to pixels for QFont.
    qreal letterSpacingEm = 0;
    bool uppercase = false;
};

TextStyle makeStyle(const StyleSpec& spec)
{
    QFont font(spec.family);
    const int wholePixels = static_cast<int>(spec.pixelSize);
    if (static_cast<qreal>(wholePixels) == spec.pixelSize) {
        font.setPixelSize(wholePixels);
    } else {
        // QFont has no fractional pixel size. Qt uses 96 logical dots per
        // inch on every screen, so 1pt is 4/3 px and this is exact.
        font.setPointSizeF(spec.pixelSize * 0.75);
    }
    font.setWeight(static_cast<QFont::Weight>(spec.weight));
    if (spec.letterSpacingEm != 0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, spec.letterSpacingEm * spec.pixelSize);
    }
    if (spec.uppercase) {
        font.setCapitalization(QFont::AllUppercase);
    }
    return TextStyle{font, spec.lineHeight};
}

} // namespace

FontFamilies FontFamilies::resolve()
{
    return FontFamilies{
        .text = FontLoader::firstAvailableFamily(
            {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}),
        .display = FontLoader::firstAvailableFamily(
            {QStringLiteral("Segoe UI Variable Display"), QStringLiteral("Segoe UI")}),
        .expressive = FontLoader::firstAvailableFamily(
            {QStringLiteral("Mr De Haviland"), QStringLiteral("Segoe Script")}),
    };
}

TextStyles TextStyles::build(const FontFamilies& families)
{
    const QString& text = families.text;
    const QString& display = families.display;
    return TextStyles{
        .body = makeStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .sectionTitle = makeStyle(
            {.family = display, .pixelSize = 20, .weight = kBold, .lineHeight = 26}),
        .sidebarSectionLabel = makeStyle({.family = text,
                                          .pixelSize = 11,
                                          .weight = kBold,
                                          .lineHeight = 16,
                                          .letterSpacingEm = 0.16,
                                          .uppercase = true}),
        .profileName = makeStyle({.family = text, .pixelSize = 14, .weight = kSemiBold}),
        .playerBarTitle = makeStyle(
            {.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 18}),
        .playerBarArtist = makeStyle(
            {.family = text, .pixelSize = 12, .weight = kRegular, .lineHeight = 16}),
        .trackTitle = makeStyle(
            {.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 20}),
        .trackArtist = makeStyle(
            {.family = text, .pixelSize = 12.5, .weight = kRegular, .lineHeight = 18}),
        .modalTitle = makeStyle(
            {.family = display, .pixelSize = 20, .weight = kSemiBold, .lineHeight = 26}),
        .settingsHeading = makeStyle({.family = display,
                                      .pixelSize = 32,
                                      .weight = kBold,
                                      .lineHeight = 40,
                                      .letterSpacingEm = -0.01}),
        .nowPlayingTitle = makeStyle(
            {.family = display, .pixelSize = 20, .weight = kBold, .lineHeight = 26}),
        // line-height: 1.2 of 30px.
        .fullScreenTitle = makeStyle(
            {.family = display, .pixelSize = 30, .weight = kBold, .lineHeight = 36}),
        // The greeting's name line: 44px, line-height 1.3, weight 750.
        .homeGreeting = makeStyle({.family = display,
                                   .pixelSize = 44,
                                   .weight = kGreeting,
                                   .lineHeight = 44 * 1.3,
                                   .letterSpacingEm = -0.015}),
    };
}

} // namespace lmp::ui
