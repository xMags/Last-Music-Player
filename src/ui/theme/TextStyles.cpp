#include "ui/theme/TextStyles.h"

#include <QGuiApplication>
#include <QStringList>

#include <algorithm>

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
constexpr int kBlack = 900;
constexpr int kBubble = 650;

} // namespace

qreal systemTextScale()
{
#ifndef NDEBUG
    // Debug builds: a size to look at without changing Windows' setting.
    bool overridden = false;
    const qreal forced = qEnvironmentVariable("LMP_TEXT_SCALE").toDouble(&overridden);
    if (overridden && forced >= 1.0 && forced <= 2.25) {
        return forced;
    }
#endif
    // Qt's application font is Windows' message font: 9 pt at 100%, larger
    // when "Make text bigger" is raised (up to 225%).
    const qreal points = QGuiApplication::font().pointSizeF();
    return points > 0 ? std::clamp(points / 9.0, 1.0, 2.25) : 1.0;
}

TextStyle makeTextStyle(const TextStyleSpec& unscaled)
{
    TextStyleSpec spec = unscaled;
    const qreal scale = systemTextScale();
    spec.pixelSize *= scale;
    spec.lineHeight *= scale;
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
    // Segoe UI Variable keeps every weight in one variable font, and the font
    // matcher only offers its Regular and Bold faces: weight 600 came out
    // Bold and 500 Regular. Setting the weight axis draws the exact weight;
    // fonts without that axis ignore it.
    font.setVariableAxis(QFont::Tag("wght"), static_cast<float>(spec.weight));
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
        // The bundled Archivo instance names itself "Archivo SemiBold"
        // although it is the width 125, weight 900 cut.
        .sleeveDisplay = FontLoader::firstAvailableFamily(
            {QStringLiteral("Archivo SemiBold"), QStringLiteral("Arial Black")}),
        .sleeveMono = FontLoader::firstAvailableFamily(
            {QStringLiteral("JetBrains Mono"), QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}),
        .mono = FontLoader::firstAvailableFamily({QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")}),
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
        // .settings-section-head h2, .settings-row-title, .settings-row-desc
        // and .settings-theme-tile (views.css 1128-1165, 1293-1303).
        .settingsSection = makeTextStyle({.family = display, .pixelSize = 16, .weight = kBold, .lineHeight = 22}),
        .settingsRowTitle = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 20}),
        .settingsRowDescription = makeTextStyle({.family = text, .pixelSize = 12, .weight = kRegular, .lineHeight = 17}),
        .settingsTileLabel = makeTextStyle({.family = text, .pixelSize = 11, .weight = kSemiBold}),
        // The account hero (views.css 1280-1288): .settings-hero-name, the
        // avatar's letter and .settings-tier.
        .settingsHeroName = makeTextStyle({.family = text, .pixelSize = 17, .weight = kSemiBold, .lineHeight = 24}),
        .settingsAvatarLetter = makeTextStyle({.family = text, .pixelSize = 20, .weight = kSemiBold}),
        .settingsTier = makeTextStyle(
            {.family = text, .pixelSize = 11, .weight = kBold, .lineHeight = 16, .letterSpacingEm = 0.12}),
        // .settings-dialog p: 14px at line-height 1.5.
        .settingsDialogText = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular, .lineHeight = 21}),
        // .settings-combo (13px, 400) and .settings-dialog-state (12.5px).
        .settingsComboLabel = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular}),
        .settingsDialogState = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kRegular}),
        // .settings-feedback: 12px at line-height 1.45.
        .settingsFeedback = makeTextStyle({.family = text, .pixelSize = 12, .weight = kRegular, .lineHeight = 12 * 1.45}),
        // Languages (views.css 1410-1560): the lead, pills and options at
        // 13px/18px, the group titles (11px, 700, 0.1em, 16px) and the
        // native names (12px).
        .settingsLanguageText = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular, .lineHeight = 18}),
        .settingsLanguageGroup = makeTextStyle(
            {.family = text, .pixelSize = 11, .weight = kBold, .lineHeight = 16, .letterSpacingEm = 0.1}),
        .settingsLanguageNative = makeTextStyle({.family = text, .pixelSize = 12, .weight = kRegular}),
        // The sign-in dialog (auth.css 307-510): .auth-title, .auth-meta-foot,
        // .auth-tagline, .auth-btn and .auth-err.
        .signInTitle = makeTextStyle({.family = families.sleeveDisplay,
                                      .pixelSize = 52,
                                      .weight = kBlack,
                                      .lineHeight = 52,
                                      .letterSpacingEm = -0.015}),
        .signInLabel = makeTextStyle(
            {.family = families.sleeveMono, .pixelSize = 11, .weight = kMedium, .letterSpacingEm = 0.16}),
        .signInTagline = makeTextStyle({.family = text, .pixelSize = 15, .weight = kRegular, .lineHeight = 21}),
        .signInButton = makeTextStyle({.family = text, .pixelSize = 16, .weight = kSemiBold}),
        .signInError = makeTextStyle({.family = text, .pixelSize = 13.5, .weight = kRegular, .lineHeight = 19.5}),
        // .tr-num and .tr-dur, .tr-album (views.css 202-240); .lib-name and
        // .lib-meta (views.css 292-299).
        .trackNumber = makeTextStyle({.family = text, .pixelSize = 13, .weight = kMedium, .tabularNumbers = true}),
        .trackAlbum = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular}),
        .cardTitle = makeTextStyle({.family = text, .pixelSize = 14, .weight = kSemiBold, .lineHeight = 22}),
        .cardSubtitle = makeTextStyle({.family = text, .pixelSize = 12, .weight = kRegular, .lineHeight = 18}),
        // .section-link (styles.css 503-511); the empty and error states
        // (views.css 124-134: strong 17px, text 14px).
        .sectionLink = makeTextStyle({.family = text, .pixelSize = 12, .weight = kSemiBold}),
        .stateTitle = makeTextStyle({.family = text, .pixelSize = 17, .weight = kSemiBold}),
        .stateText = makeTextStyle({.family = text, .pixelSize = 14, .weight = kRegular, .lineHeight = 19.6}),
        .tableHeading = makeTextStyle({.family = text, .pixelSize = 11, .weight = kBold, .letterSpacingEm = 0.08}),
        // .view-title-block .eyebrow (views.css 13-19); .section-pill
        // (styles.css 488-501); the genre tile's label (GenreArtworkTile.cpp).
        .eyebrow = makeTextStyle({.family = text, .pixelSize = 11, .weight = kBold, .letterSpacingEm = 0.16}),
        .pill = makeTextStyle(
            {.family = text, .pixelSize = 10, .weight = kSemiBold, .letterSpacingEm = 0.04, .uppercase = true}),
        .genreLabel = makeTextStyle(
            {.family = families.mono, .pixelSize = 11.5, .weight = kRegular, .letterSpacingEm = 0.14}),
        // The collection header (views.css 361-503): the back link, eyebrow,
        // title (26 to 44px with the card's width; pages scale this 44px
        // face down), info line, description and the Play pill.
        .collectionBack = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kBubble}),
        .collectionEyebrow = makeTextStyle({.family = text, .pixelSize = 11, .weight = kBold, .letterSpacingEm = 0.12}),
        .collectionTitle = makeTextStyle(
            {.family = display, .pixelSize = 44, .weight = kExtraBold, .lineHeight = 44 * 1.08, .letterSpacingEm = -0.02}),
        .collectionInfo = makeTextStyle({.family = text, .pixelSize = 13, .weight = kRegular}),
        .collectionInfoStrong = makeTextStyle({.family = text, .pixelSize = 13, .weight = kSemiBold}),
        .collectionDescription = makeTextStyle({.family = text, .pixelSize = 13.5, .weight = kRegular, .lineHeight = 13.5 * 1.5}),
        .collectionPlay = makeTextStyle({.family = text, .pixelSize = 14, .weight = kBold}),
        // Home (styles.css 522-552, 818-842, 2432-2460, 2575-2600): the salute
        // over the name and the tier labels, the "Jump back in" title and
        // artist, a shortcut's name, and a mix card's badge and line.
        .homeLabel = makeTextStyle(
            {.family = text, .pixelSize = 12, .weight = kBold, .letterSpacingEm = 0.14, .uppercase = true}),
        .heroTitle = makeTextStyle(
            {.family = display, .pixelSize = 34, .weight = kGreeting, .lineHeight = 34 * 1.15, .letterSpacingEm = -0.01}),
        .heroSub = makeTextStyle({.family = text, .pixelSize = 15, .weight = kRegular}),
        .quickTitle = makeTextStyle({.family = text, .pixelSize = 14, .weight = kBubble}),
        .mixBadge = makeTextStyle(
            {.family = text, .pixelSize = 10.5, .weight = kExtraBold, .letterSpacingEm = 0.18, .uppercase = true}),
        .mixBlurb = makeTextStyle({.family = text, .pixelSize = 12.5, .weight = kMedium, .lineHeight = 12.5 * 1.4}),
        // Browse (views.css 1855-2030): a category tile's name, the top
        // result's title and the small pill naming what it is.
        .searchCategory = makeTextStyle({.family = text, .pixelSize = 16, .weight = kBold, .lineHeight = 16 * 1.25}),
        .searchTopTitle = makeTextStyle(
            {.family = display, .pixelSize = 28, .weight = kBold, .lineHeight = 28 * 1.12, .letterSpacingEm = -0.01}),
        .searchTopKind = makeTextStyle({.family = text, .pixelSize = 12, .weight = kSemiBold, .letterSpacingEm = 0.02}),
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
        // .settings-button.small: 12px.
        .settingsButtonSmallLabel = makeTextStyle({.family = text, .pixelSize = 12, .weight = kSemiBold}),
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
        // .np-artist: 13px, 500, line-height 1.35.
        .nowPlayingArtist = makeTextStyle(
            {.family = text, .pixelSize = 13, .weight = kMedium, .lineHeight = 13 * 1.35}),
        // .queue-dur: 12px, tabular figures.
        .queueDuration = makeTextStyle(
            {.family = text, .pixelSize = 12, .weight = kRegular, .tabularNumbers = true}),
        // .queue-clear: 10.5px, 600, 0.04em, uppercase.
        .queueAction = makeTextStyle({.family = text,
                                      .pixelSize = 10.5,
                                      .weight = kSemiBold,
                                      .letterSpacingEm = 0.04,
                                      .uppercase = true}),
        // .queue-radio-status: 10.5px.
        .queueStatus = makeTextStyle({.family = text, .pixelSize = 10.5, .weight = kRegular}),
        // .empty-state: 13px, 600.
        .emptyStateCompact = makeTextStyle({.family = text, .pixelSize = 13, .weight = kSemiBold}),
    };
}

} // namespace lmp::ui
