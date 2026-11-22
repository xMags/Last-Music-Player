#include <QFont>
#include <QFontDatabase>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "ui/theme/FontLoader.h"
#include "ui/theme/Theme.h"

class FontsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void bundledFontLoads();
    void textStylesMatchReference();
    void fractionalSizeKeepsExactPixels();
    void familiesFollowTheStacks();

private:
    QStringList m_loaded;
};

void FontsTest::initTestCase()
{
    m_loaded = lmp::ui::FontLoader::loadBundledFonts();
}

void FontsTest::bundledFontLoads()
{
    QVERIFY(m_loaded.contains(QStringLiteral("Mr De Haviland")));
    QVERIFY(QFontDatabase::families().contains(QStringLiteral("Mr De Haviland")));
    const lmp::ui::Theme theme;
    QCOMPARE(theme.fontExpressive(), QStringLiteral("Mr De Haviland"));
}

void FontsTest::textStylesMatchReference()
{
    const lmp::ui::Theme theme;
    // .section-title: 20px / 26px, weight 700.
    QCOMPARE(theme.sectionTitle().pixelSize(), 20);
    QCOMPARE(theme.sectionTitle().weight(), QFont::Bold);
    QCOMPARE(theme.lineHeightSectionTitle(), 26.0);
    // .pb-title: 14px / 18px, weight 600.
    QCOMPARE(theme.playerBarTitle().pixelSize(), 14);
    QCOMPARE(theme.playerBarTitle().weight(), QFont::DemiBold);
    QCOMPARE(theme.lineHeightPlayerBarTitle(), 18.0);
    // .settings-head h1: 32px / 40px, weight 700, letter-spacing -0.01em.
    QCOMPARE(theme.settingsHeading().pixelSize(), 32);
    QCOMPARE(theme.settingsHeading().weight(), QFont::Bold);
    QCOMPARE(theme.lineHeightSettingsHeading(), 40.0);
    // -0.01em of 32px is -0.32px; QFont stores spacing in 1/64 px steps.
    QVERIFY(qAbs(theme.settingsHeading().letterSpacing() - (-0.32)) <= 1.0 / 64);
    // .sidebar-section-label: 11px / 16px, 700, 0.16em, uppercase.
    QCOMPARE(theme.sidebarSectionLabel().pixelSize(), 11);
    QCOMPARE(theme.sidebarSectionLabel().capitalization(), QFont::AllUppercase);
    // .home-greeting-name: weight 750, line height 1.3 of 44px.
    QCOMPARE(static_cast<int>(theme.homeGreeting().weight()), 750);
    QCOMPARE(theme.lineHeightHomeGreeting(), 44 * 1.3);
}

void FontsTest::fractionalSizeKeepsExactPixels()
{
    const lmp::ui::Theme theme;
    // .tr-artist is 12.5px, stored as points because QFont pixel sizes are
    // whole numbers: 12.5 * 0.75 = 9.375pt.
    QCOMPARE(theme.trackArtist().pointSizeF(), 9.375);
    QCOMPARE(theme.lineHeightTrackArtist(), 18.0);
}

void FontsTest::familiesFollowTheStacks()
{
    const lmp::ui::Theme theme;
    const QStringList families = QFontDatabase::families();
    const QString expectedText = families.contains(QStringLiteral("Segoe UI Variable Text"))
        ? QStringLiteral("Segoe UI Variable Text")
        : QStringLiteral("Segoe UI");
    QCOMPARE(theme.fontText(), expectedText);
    QCOMPARE(theme.sectionTitle().family(), theme.fontDisplay());
    QCOMPARE(theme.trackTitle().family(), theme.fontText());
}

QTEST_MAIN(FontsTest)
#include "tst_fonts.moc"
