#include <QColor>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariantList>

#include <cmath>

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

QTEST_MAIN(ThemeTest)
#include "tst_theme.moc"
