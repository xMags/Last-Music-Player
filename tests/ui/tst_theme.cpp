#include <QColor>
#include <QFont>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <cstdlib>
#include <QVariantList>

#include <cmath>

#include "ui/theme/ShadowLayer.h"
#include "ui/theme/Theme.h"

using lmp::ui::Theme;

namespace {

// Compares the 8-bit channels and the alpha to two decimals, which is the
// precision the design tokens are written in.
bool sameColor(const QColor& actual, const QColor& expected)
{
    return actual.red() == expected.red() && actual.green() == expected.green()
        && actual.blue() == expected.blue()
        && std::abs(actual.alphaF() - expected.alphaF()) < 0.005;
}

QColor rgba(int red, int green, int blue, float alpha)
{
    QColor color(red, green, blue);
    color.setAlphaF(alpha);
    return color;
}

QString describe(const QColor& color)
{
    return color.name(QColor::HexArgb);
}

} // namespace

class ThemeTest : public QObject {
    Q_OBJECT

private slots:
    void lightTokens();
    void darkTokens();
    void accentsMatchBrand();
    void primaryFollowsTheme();
    void mixSrgbMatchesHandValues();
    void mixOklabMatchesHandValues();
    void switchingEmitsOneSignal();
    void artworkGradients();
    void rippleColoursFollowTheSurface();
    void ambientDefaultsToTheAccentSeed();
    void fullscreenTokensMatchReference();
    void queueTokensMatchReference();
    void skeletonsAreTheAccentTintedIntoTheSurface();
};

// A macro because QVERIFY2 has to expand inside the test function to end it on
// failure; a helper function could not stop the test.
#define LMP_CHECK_COLOR(actual, expected) \
    QVERIFY2(sameColor((actual), (expected)), qPrintable(describe(actual)))

void ThemeTest::lightTokens()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    QVERIFY(!theme.dark());
    LMP_CHECK_COLOR(theme.surface(), QColor(QStringLiteral("#FFFFFF")));
    LMP_CHECK_COLOR(theme.surfaceAlt(), QColor(QStringLiteral("#FBFBFD")));
    LMP_CHECK_COLOR(theme.canvas(), QColor(QStringLiteral("#F3F3F3")));
    LMP_CHECK_COLOR(theme.neutral(), QColor(QStringLiteral("#F3F4F6")));
    LMP_CHECK_COLOR(theme.grad1(), QColor(QStringLiteral("#F0FCFF")));
    LMP_CHECK_COLOR(theme.grad2(), QColor(QStringLiteral("#F3F0FE")));
    LMP_CHECK_COLOR(theme.stroke(), rgba(0, 0, 0, 0.06f));
    LMP_CHECK_COLOR(theme.strokeStrong(), rgba(0, 0, 0, 0.10f));
    LMP_CHECK_COLOR(theme.rowHover(), rgba(0, 0, 0, 0.024f));
    LMP_CHECK_COLOR(theme.controlHover(), rgba(0, 0, 0, 0.04f));
    LMP_CHECK_COLOR(theme.fg1(), QColor(QStringLiteral("#1A1A1A")));
    LMP_CHECK_COLOR(theme.fg2(), QColor(QStringLiteral("#5C5C5C")));
    LMP_CHECK_COLOR(theme.fg3(), QColor(QStringLiteral("#8A8A8A")));
    LMP_CHECK_COLOR(theme.fg4(), QColor(QStringLiteral("#B4B4B4")));
    LMP_CHECK_COLOR(theme.playBg(), QColor(QStringLiteral("#1A202C")));
    LMP_CHECK_COLOR(theme.danger(), QColor(QStringLiteral("#D83B3B")));
    LMP_CHECK_COLOR(theme.onPrimary(), QColor(QStringLiteral("#FFFFFF")));
    LMP_CHECK_COLOR(theme.artGrad1(), QColor(QStringLiteral("#F3EDFC")));
}

// Loading placeholders: the teal accent (#0097B2) at 10%, 14% and 33% over
// white on light, as the design's mock-up gives them; on dark the dark accent
// over the dark surface, a little lighter than it.
void ThemeTest::skeletonsAreTheAccentTintedIntoTheSurface()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    theme.setAccent(Theme::Accent::Teal);
    const auto near = [](const QColor& actual, const char* expected) {
        const QColor want(QString::fromLatin1(expected));
        return std::abs(actual.red() - want.red()) <= 2 && std::abs(actual.green() - want.green()) <= 2
            && std::abs(actual.blue() - want.blue()) <= 2;
    };
    QVERIFY2(near(theme.skeleton(), "#E6F4F7"), qPrintable(theme.skeleton().name()));
    QVERIFY2(near(theme.skeletonStrong(), "#DDEFF3"), qPrintable(theme.skeletonStrong().name()));
    QVERIFY2(near(theme.skeletonGlyph(), "#ABDCE5"), qPrintable(theme.skeletonGlyph().name()));

    theme.setMode(Theme::ThemeMode::Dark);
    QVERIFY(theme.skeleton().lightness() > theme.surface().lightness());
    QVERIFY(theme.skeleton().lightness() - theme.surface().lightness() < 20);
}

void ThemeTest::darkTokens()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Dark);
    QVERIFY(theme.dark());
    LMP_CHECK_COLOR(theme.surface(), QColor(QStringLiteral("#2B2B2B")));
    LMP_CHECK_COLOR(theme.surfaceAlt(), QColor(QStringLiteral("#272727")));
    LMP_CHECK_COLOR(theme.canvas(), QColor(QStringLiteral("#202020")));
    LMP_CHECK_COLOR(theme.neutral(), QColor(QStringLiteral("#3A3A3A")));
    LMP_CHECK_COLOR(theme.grad1(), QColor(QStringLiteral("#1D2329")));
    LMP_CHECK_COLOR(theme.grad2(), QColor(QStringLiteral("#21202B")));
    LMP_CHECK_COLOR(theme.stroke(), rgba(255, 255, 255, 0.07f));
    LMP_CHECK_COLOR(theme.divider(), rgba(255, 255, 255, 0.08f));
    LMP_CHECK_COLOR(theme.segmentTrack(), rgba(255, 255, 255, 0.06f));
    LMP_CHECK_COLOR(theme.segmentSelected(), rgba(255, 255, 255, 0.12f));
    LMP_CHECK_COLOR(theme.fg1(), QColor(QStringLiteral("#FFFFFF")));
    LMP_CHECK_COLOR(theme.fg2(), QColor(QStringLiteral("#C8C8C8")));
    LMP_CHECK_COLOR(theme.fg4(), QColor(QStringLiteral("#6E6E6E")));
    LMP_CHECK_COLOR(theme.playBg(), QColor(QStringLiteral("#EDEDED")));
    LMP_CHECK_COLOR(theme.playFg(), QColor(QStringLiteral("#1A1A1A")));
    LMP_CHECK_COLOR(theme.danger(), QColor(QStringLiteral("#FF6B6B")));
    LMP_CHECK_COLOR(theme.onPrimary(), QColor(QStringLiteral("#101415")));
    LMP_CHECK_COLOR(theme.artGrad2(), QColor(QStringLiteral("#1f1d2a")));
}

void ThemeTest::accentsMatchBrand()
{
    struct Expected {
        Theme::Accent accent;
        const char* seed;
        const char* light;
        const char* dark;
    };
    const Expected expected[] = {
        {Theme::Accent::Teal, "#0097B2", "#00687A", "#54D6EC"},
        {Theme::Accent::Purple, "#6A5CD6", "#5044B2", "#C8BFFF"},
        {Theme::Accent::Coral, "#E0574A", "#A63B32", "#FFB4AA"},
        {Theme::Accent::Green, "#1F9D6B", "#0D7049", "#77DDAA"},
        {Theme::Accent::Amber, "#D88A2E", "#8A5300", "#FFC173"},
    };
    Theme theme;
    QCOMPARE(theme.accent(), Theme::Accent::Teal);
    for (const Expected& row : expected) {
        theme.setAccent(row.accent);
        LMP_CHECK_COLOR(theme.accentSeed(), QColor(QLatin1StringView(row.seed)));
        LMP_CHECK_COLOR(theme.accentLight(), QColor(QLatin1StringView(row.light)));
        LMP_CHECK_COLOR(theme.accentDark(), QColor(QLatin1StringView(row.dark)));
    }
    QCOMPARE(Theme::accentSeedList().size(), 5);
}

void ThemeTest::primaryFollowsTheme()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    LMP_CHECK_COLOR(theme.primary(), QColor(QStringLiteral("#00687A")));
    // --accent-soft: color-mix(in srgb, primary 12%, transparent) keeps the
    // primary's channels and sets alpha 0.12.
    LMP_CHECK_COLOR(theme.accentSoft(), rgba(0x00, 0x68, 0x7A, 0.12f));
    LMP_CHECK_COLOR(theme.accentSoft2(), rgba(0x00, 0x68, 0x7A, 0.20f));
    LMP_CHECK_COLOR(theme.accentSel(), rgba(0x00, 0x68, 0x7A, 0.08f));
    theme.setMode(Theme::ThemeMode::Dark);
    LMP_CHECK_COLOR(theme.primary(), QColor(QStringLiteral("#54D6EC")));
}

void ThemeTest::mixSrgbMatchesHandValues()
{
    Theme theme;
    // Half red, half blue in sRGB: each channel 255 * 0.5 = 127.5, rounds to 128.
    LMP_CHECK_COLOR(theme.mixSrgb(QColor(255, 0, 0), QColor(0, 0, 255), 0.5),
                    QColor(128, 0, 128));
    // 25% white over black: 255 * 0.25 = 63.75, rounds to 64.
    LMP_CHECK_COLOR(theme.mixSrgb(Qt::white, Qt::black, 0.25), QColor(64, 64, 64));
}

void ThemeTest::mixOklabMatchesHandValues()
{
    Theme theme;
    // Worked through the OKLab formulas by hand (linearise, LMS, cube root,
    // Lab; average; invert): red and blue at 50% give rgb(140.36, 83.03, 162.31).
    LMP_CHECK_COLOR(theme.mixOklab(QColor(255, 0, 0), QColor(0, 0, 255), 0.5),
                    QColor(140, 83, 162));
    // --accent-fill-2 for teal: #0097B2 72% with white gives
    // rgb(104.86, 180.16, 199.71).
    theme.setAccent(Theme::Accent::Teal);
    LMP_CHECK_COLOR(theme.accentFill2(), QColor(105, 180, 200));
}

void ThemeTest::switchingEmitsOneSignal()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    QSignalSpy spy(&theme, &Theme::changed);
    theme.setMode(Theme::ThemeMode::Dark);
    theme.setMode(Theme::ThemeMode::Dark);
    theme.setAccent(Theme::Accent::Coral);
    QCOMPARE(spy.count(), 2);
}

void ThemeTest::artworkGradients()
{
    const QVariantList gradients = Theme::artworkGradientList();
    QCOMPARE(gradients.size(), 9);
    const QVariantList first = gradients.first().toList();
    QCOMPARE(first.size(), 2);
    LMP_CHECK_COLOR(first.at(0).value<QColor>(), QColor(QStringLiteral("#8B7BE8")));
    LMP_CHECK_COLOR(gradients.last().toList().at(1).value<QColor>(),
                    QColor(QStringLiteral("#007A86")));
}

void ThemeTest::rippleColoursFollowTheSurface()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    // White has luminance 255: a light surface gets the faint shadow disc and
    // the accent ring at 75%.
    LMP_CHECK_COLOR(theme.rippleFillOn(Qt::white), rgba(0, 0, 0, 0.12f));
    LMP_CHECK_COLOR(theme.rippleRingOn(Qt::white), rgba(0x00, 0x68, 0x7A, 0.75f));
    // #1A202C: 0.2126*26 + 0.7152*32 + 0.0722*44 = 31.6, below 140, so dark.
    const QColor playBg(QStringLiteral("#1A202C"));
    LMP_CHECK_COLOR(theme.rippleFillOn(playBg), rgba(255, 255, 255, 0.2f));
    LMP_CHECK_COLOR(theme.rippleRingOn(playBg), rgba(255, 255, 255, 0.55f));
    // A transparent surface follows the theme.
    LMP_CHECK_COLOR(theme.rippleFillOn(Qt::transparent), rgba(0, 0, 0, 0.12f));
    theme.setMode(Theme::ThemeMode::Dark);
    LMP_CHECK_COLOR(theme.rippleFillOn(Qt::transparent), rgba(255, 255, 255, 0.2f));
}

void ThemeTest::ambientDefaultsToTheAccentSeed()
{
    Theme theme;
    theme.setMode(Theme::ThemeMode::Light);
    theme.setAccent(Theme::Accent::Teal);
    QCOMPARE(theme.ambient(), QColor(QStringLiteral("#0097B2")));
    QCOMPARE(theme.ambientStrength(), 1.0);

    QSignalSpy spy(&theme, &Theme::ambientChanged);
    theme.setAccent(Theme::Accent::Coral);
    QCOMPARE(theme.ambient(), QColor(QStringLiteral("#E0574A")));
    QCOMPARE(spy.count(), 1);

    const QColor song(QStringLiteral("#336699"));
    theme.setAmbient(song);
    QCOMPARE(theme.ambient(), song);
    theme.setAccent(Theme::Accent::Purple);
    QCOMPARE(theme.ambient(), song);
    theme.resetAmbient();
    QCOMPARE(theme.ambient(), QColor(QStringLiteral("#6A5CD6")));

    theme.setMode(Theme::ThemeMode::Dark);
    QCOMPARE(theme.ambientStrength(), 1.45);
}

void ThemeTest::fullscreenTokensMatchReference()
{
    // styles.css 2089-2339: the same in both themes.
    Theme theme;
    for (const Theme::ThemeMode mode : {Theme::ThemeMode::Light, Theme::ThemeMode::Dark}) {
        theme.setMode(mode);
        LMP_CHECK_COLOR(theme.fullscreenBackground(), QColor(QStringLiteral("#0E1016")));
        LMP_CHECK_COLOR(theme.fullscreenText(), QColor(QStringLiteral("#FFFFFF")));
        LMP_CHECK_COLOR(theme.fullscreenArtistText(), rgba(255, 255, 255, 0.75f));
        LMP_CHECK_COLOR(theme.fullscreenMetaText(), rgba(255, 255, 255, 0.55f));
        LMP_CHECK_COLOR(theme.fullscreenTimeText(), rgba(255, 255, 255, 0.6f));
        LMP_CHECK_COLOR(theme.fullscreenButtonGlyph(), rgba(255, 255, 255, 0.85f));
        LMP_CHECK_COLOR(theme.fullscreenButtonHover(), rgba(255, 255, 255, 0.12f));
        LMP_CHECK_COLOR(theme.fullscreenPlayGlyph(), QColor(QStringLiteral("#101216")));
        LMP_CHECK_COLOR(theme.fullscreenSeekTrack(), rgba(255, 255, 255, 0.22f));
        LMP_CHECK_COLOR(theme.fullscreenTabsFill(), rgba(255, 255, 255, 0.07f));
        LMP_CHECK_COLOR(theme.fullscreenTabsBorder(), rgba(255, 255, 255, 0.1f));
        LMP_CHECK_COLOR(theme.fullscreenTabText(), rgba(255, 255, 255, 0.7f));
        LMP_CHECK_COLOR(theme.fullscreenTabActiveFill(), rgba(255, 255, 255, 0.16f));
        LMP_CHECK_COLOR(theme.fullscreenPanelFill(), rgba(8, 10, 16, 0.34f));
        LMP_CHECK_COLOR(theme.fullscreenPanelBorder(), rgba(255, 255, 255, 0.08f));
        LMP_CHECK_COLOR(theme.fullscreenBackdropEnd(), QColor(QStringLiteral("#1A2340")));
        // brightness(0.42): black over the picture at 58%.
        LMP_CHECK_COLOR(theme.fullscreenBackdropDim(), rgba(0, 0, 0, 0.58f));
    }
    QCOMPARE(theme.fullscreenBackdropBlur(), 56);
    QCOMPARE(theme.fullscreenBackdropScale(), 1.1);
    QCOMPARE(theme.fullscreenBackdropInset(), 60);
    QCOMPARE(theme.fullscreenArtRadius(), 20);
    QCOMPARE(theme.fullscreenPanelRadius(), 20);
    QCOMPARE(theme.fullscreenArtist().pixelSize(), 16);
    QCOMPARE(theme.lineHeightFullscreenArtist(), 21.6);
    QCOMPARE(theme.fullscreenTime().pixelSize(), 12);
    QCOMPARE(theme.fullscreenTabActive().weight(), QFont::DemiBold);
    QCOMPARE(theme.fullscreenRepeatBadge().pixelSize(), 9);
    const QVariantList art = theme.shadowFullscreenArt();
    QCOMPARE(art.size(), 1);
    const auto layer = art.first().value<lmp::ui::ShadowLayer>();
    QCOMPARE(layer.offsetY, 30.0);
    QCOMPARE(layer.blur, 80.0);
    LMP_CHECK_COLOR(layer.color, rgba(0, 0, 0, 0.55f));
}

void ThemeTest::queueTokensMatchReference()
{
    Theme theme;
    // styles.css 1902-1912: Clear is a deeper red in light mode.
    theme.setMode(Theme::ThemeMode::Light);
    LMP_CHECK_COLOR(theme.queueClear(), QColor(QStringLiteral("#E00000")));
    LMP_CHECK_COLOR(theme.queueClearHover(), QColor(QStringLiteral("#B80000")));
    theme.setMode(Theme::ThemeMode::Dark);
    LMP_CHECK_COLOR(theme.queueClear(), QColor(QStringLiteral("#FF0000")));
    LMP_CHECK_COLOR(theme.queueClearHover(), QColor(QStringLiteral("#FF4D4D")));
    // styles.css 2108: --hover inside the full-screen player.
    LMP_CHECK_COLOR(theme.fullscreenRowHover(), rgba(255, 255, 255, 0.08f));

    // .np-artist 13px 500, line-height 1.35; .queue-dur 12px; .queue-clear
    // 10.5px 600 with 0.04em, uppercase; .queue-radio-status 10.5px;
    // .empty-state 13px 600.
    QCOMPARE(theme.nowPlayingArtist().pixelSize(), 13);
    QCOMPARE(theme.nowPlayingArtist().weight(), QFont::Medium);
    QCOMPARE(theme.lineHeightNowPlayingArtist(), 13 * 1.35);
    QCOMPARE(theme.queueDuration().pixelSize(), 12);
    QCOMPARE(theme.queueAction().pointSizeF(), 10.5 * 0.75);
    QCOMPARE(theme.queueAction().weight(), QFont::DemiBold);
    QCOMPARE(theme.queueAction().capitalization(), QFont::AllUppercase);
    // QFont keeps letter spacing in 1/64 px steps.
    QVERIFY(qAbs(theme.queueAction().letterSpacing() - 10.5 * 0.04) <= 1.0 / 64);
    QCOMPARE(theme.queueStatus().pointSizeF(), 10.5 * 0.75);
    QCOMPARE(theme.emptyStateCompact().pixelSize(), 13);
    QCOMPARE(theme.emptyStateCompact().weight(), QFont::DemiBold);
}

QTEST_MAIN(ThemeTest)
#include "tst_theme.moc"
