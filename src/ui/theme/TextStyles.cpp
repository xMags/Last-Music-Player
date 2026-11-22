#include "ui/theme/TextStyles.h"

#include <QStringList>

#include "ui/theme/FontLoader.h"

namespace lmp::ui {

namespace {

// Weights use the CSS 100 to 1000 scale, which QFont shares.
constexpr int kRegular = 400;
constexpr int kMedium = 500;
constexpr int kSemiBold = 600;
constexpr int kBold = 700;
constexpr int kGreeting = 750;
constexpr int kExtraBold = 800;
constexpr int kBubble = 650;

} // namespace

TextStyle makeTextStyle(const TextStyleSpec& spec)
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
    if (spec.tabularNumbers) {
        font.setFeature(QFont::Tag("tnum"), 1);
    }
    return TextStyle{font, spec.lineHeight};
}

FontFamilies FontFamilies::resolve()
{
    return FontFamilies{
        .text = FontLoader::firstAvailableFamily(
            {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI")}),
        .display = FontLoader::firstAvailableFamily(
            {QStringLiteral("Segoe UI Variable Display"), QStringLiteral("Segoe UI")}),
        .expressive = FontLoader::firstAvailableFamily(
            {QStringLiteral("Mr De Haviland"), QStringLiteral("Segoe Script")}),
        .captionIcons = FontLoader::firstAvailableFamily(
            {QStringLiteral("Segoe Fluent Icons"), QStringLiteral("Segoe MDL2 Assets")}),
    };
}

TextStyles TextStyles::build(const FontFamilies& families)
{
    const QString& text = families.text;
    const QString& display = families.display;
    return TextStyles{
        .body = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .sectionTitle = makeTextStyle(
            {.family = display, .pixelSize = 20, .weight = kBold, .lineHeight = 26}),
        .sidebarSectionLabel = makeTextStyle({.family = text,
                                          .pixelSize = 11,
                                          .weight = kBold,
                                          .lineHeight = 16,
                                          .letterSpacingEm = 0.16,
                                          .uppercase = true}),
        .profileName = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold}),
        .playerBarTitle = makeTextStyle(
            {.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 18}),
        .playerBarArtist = makeTextStyle(
            {.family = text, .pixelSize = 12, .weight = kRegular, .lineHeight = 16}),
        .trackTitle = makeTextStyle(
            {.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 20}),
        .trackArtist = makeTextStyle(
            {.family = text, .pixelSize = 12.5, .weight = kRegular, .lineHeight = 18}),
        .modalTitle = makeTextStyle(
            {.family = display, .pixelSize = 20, .weight = kSemiBold, .lineHeight = 26}),
        .settingsHeading = makeTextStyle({.family = display,
                                      .pixelSize = 32,
                                      .weight = kBold,
                                      .lineHeight = 40,
                                      .letterSpacingEm = -0.01}),
        .nowPlayingTitle = makeTextStyle(
            {.family = display, .pixelSize = 20, .weight = kBold, .lineHeight = 26}),
        // line-height: 1.2 of 30px.
        .fullScreenTitle = makeTextStyle(
            {.family = display, .pixelSize = 30, .weight = kBold, .lineHeight = 36}),
        // The greeting's name line: 44px, line-height 1.3, weight 750.
        .homeGreeting = makeTextStyle({.family = display,
                                   .pixelSize = 44,
                                   .weight = kGreeting,
                                   .lineHeight = 44 * 1.3,
                                   .letterSpacingEm = -0.015}),
        .buttonLabel = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold}),
        .settingsButtonLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kSemiBold}),
        .chipLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kSemiBold}),
        // .seek-bubble: line-height 1.2 of 11.5px.
        .bubbleLabel = makeTextStyle({.family = text,
                                  .pixelSize = 11.5,
                                  .weight = kBubble,
                                  .lineHeight = 11.5 * 1.2,
                                  .tabularNumbers = true}),
        .selectLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kMedium}),
        .optionLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular}),
        .searchInput = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .segmentLabel = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .segmentSelectedLabel = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold}),
        .segmentCompactLabel = makeTextStyle({.family = text, .pixelSize = 12, .weight = kSemiBold}),
        // .kbd-chip: 11.5px, weight 500, letter-spacing 0.02em.
        .shortcutLabel = makeTextStyle(
            {.family = text, .pixelSize = 11.5, .weight = kMedium, .letterSpacingEm = 0.02}),
        .menuLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular}),
        .modalDescription = makeTextStyle(
            {.family = text, .pixelSize = 14, .weight = kRegular, .lineHeight = 20}),
        // .modal-field span: 10.5px, 700, letter-spacing 0.08em, uppercase.
        .fieldLabel = makeTextStyle({.family = text,
                                 .pixelSize = 10.5,
                                 .weight = kBold,
                                 .letterSpacingEm = 0.08,
                                 .uppercase = true}),
        .fieldInput = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .errorText = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kRegular}),
        .toastLabel = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kRegular}),
        .toastAction = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kBold}),
        .artTileLabel = makeTextStyle({.family = text, .pixelSize = 22, .weight = kBold}),
        .artTileLabelBig = makeTextStyle({.family = text, .pixelSize = 48, .weight = kBold}),
        .captionGlyph =
            makeTextStyle({.family = families.captionIcons, .pixelSize = 10, .weight = kRegular}),
        .brandLabel = makeTextStyle({.family = text, .pixelSize = 12, .weight = kSemiBold}),
        .navLabel = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular}),
        .navLabelActive = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold}),
        // .profile-tier: 10px, 700, letter-spacing 0.08em.
        .profileTier = makeTextStyle(
            {.family = text, .pixelSize = 10, .weight = kBold, .letterSpacingEm = 0.08}),
        // .profile-menu-dots: 20px, line-height 1.
        .profileDots =
            makeTextStyle({.family = text, .pixelSize = 20, .weight = kRegular, .lineHeight = 20}),
        // .pb-time: 11px, 500, tabular figures.
        .playerTime = makeTextStyle(
            {.family = text, .pixelSize = 11, .weight = kMedium, .tabularNumbers = true}),
        // .repeat-one-badge: 8px, 800.
        .repeatBadge = makeTextStyle({.family = text, .pixelSize = 8, .weight = kExtraBold}),
    };
}

} // namespace lmp::ui
