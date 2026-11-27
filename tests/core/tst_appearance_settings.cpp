#include <QObject>
#include <QString>
#include <QTest>

#include "core/settings/AppearanceSettings.h"

using lmp::core::settings::accentChoiceFromStored;
using lmp::core::settings::BackdropChoice;
using lmp::core::settings::backdropChoiceFromStored;
using lmp::core::settings::storedBackdropChoice;
using lmp::core::settings::storedAccentChoice;
using lmp::core::settings::storedThemeChoice;
using lmp::core::settings::ThemeChoice;
using lmp::core::settings::themeChoiceFromStored;

// Theme and AccentColor are read and written by installed earlier versions,
// so both directions keep their strings.
class AppearanceSettingsTest : public QObject {
    Q_OBJECT

private slots:
    void themeReadsTheExactNames_data();
    void themeReadsTheExactNames();
    void themeWritesTheStoredNames();
    void accentReadsBothColourSets_data();
    void accentReadsBothColourSets();
    void accentWritesTheEarlierColours();
    void accentRoundTrips();
    void backdropReadsTheExactNames_data();
    void backdropReadsTheExactNames();
    void backdropWritesTheStoredNames();
};

void AppearanceSettingsTest::themeReadsTheExactNames_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<ThemeChoice>("expected");
    QTest::newRow("Light") << QStringLiteral("Light") << ThemeChoice::Light;
    QTest::newRow("Dark") << QStringLiteral("Dark") << ThemeChoice::Dark;
    QTest::newRow("System") << QStringLiteral("System") << ThemeChoice::System;
    QTest::newRow("missing") << QString() << ThemeChoice::System;
    // Compared exactly, as before: case and spaces count.
    QTest::newRow("lower case") << QStringLiteral("dark") << ThemeChoice::System;
    QTest::newRow("upper case") << QStringLiteral("LIGHT") << ThemeChoice::System;
    QTest::newRow("padded") << QStringLiteral(" Dark") << ThemeChoice::System;
    QTest::newRow("unknown") << QStringLiteral("Sepia") << ThemeChoice::System;
}

void AppearanceSettingsTest::themeReadsTheExactNames()
{
    QFETCH(QString, stored);
    QFETCH(ThemeChoice, expected);
    QCOMPARE(themeChoiceFromStored(stored), expected);
}

void AppearanceSettingsTest::themeWritesTheStoredNames()
{
    QCOMPARE(storedThemeChoice(ThemeChoice::Light), QStringLiteral("Light"));
    QCOMPARE(storedThemeChoice(ThemeChoice::Dark), QStringLiteral("Dark"));
    QCOMPARE(storedThemeChoice(ThemeChoice::System), QStringLiteral("System"));
}

void AppearanceSettingsTest::accentReadsBothColourSets_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<int>("expected");
    // The earlier versions' swatches, as they saved them.
    QTest::newRow("earlier teal") << QStringLiteral("#FF0097B2") << 0;
    QTest::newRow("earlier blue") << QStringLiteral("#FF6B8EFE") << 1;
    QTest::newRow("earlier pink") << QStringLiteral("#FFE0719C") << 2;
    QTest::newRow("earlier green") << QStringLiteral("#FF6FBF73") << 3;
    QTest::newRow("earlier orange") << QStringLiteral("#FFE8A33D") << 4;
    // The picker's own seeds, in either form.
    QTest::newRow("seed purple") << QStringLiteral("#6A5CD6") << 1;
    QTest::newRow("seed coral with alpha") << QStringLiteral("#FFE0574A") << 2;
    QTest::newRow("seed green") << QStringLiteral("#1F9D6B") << 3;
    QTest::newRow("seed amber") << QStringLiteral("#D88A2E") << 4;
    // Letter case and the alpha pair do not matter.
    QTest::newRow("lower case") << QStringLiteral("#ffe8a33d") << 4;
    QTest::newRow("other alpha") << QStringLiteral("#806B8EFE") << 1;
    QTest::newRow("no hash") << QStringLiteral("FF6FBF73") << 3;
    // Anything else is teal.
    QTest::newRow("missing") << QString() << 0;
    QTest::newRow("other colour") << QStringLiteral("#FF123456") << 0;
    QTest::newRow("not hex") << QStringLiteral("#FFGG97B2") << 0;
    QTest::newRow("short") << QStringLiteral("#0097B") << 0;
    QTest::newRow("name") << QStringLiteral("purple") << 0;
}

void AppearanceSettingsTest::accentReadsBothColourSets()
{
    QFETCH(QString, stored);
    QFETCH(int, expected);
    QCOMPARE(accentChoiceFromStored(stored), expected);
}

void AppearanceSettingsTest::accentWritesTheEarlierColours()
{
    QCOMPARE(storedAccentChoice(0), QStringLiteral("#FF0097B2"));
    QCOMPARE(storedAccentChoice(1), QStringLiteral("#FF6B8EFE"));
    QCOMPARE(storedAccentChoice(2), QStringLiteral("#FFE0719C"));
    QCOMPARE(storedAccentChoice(3), QStringLiteral("#FF6FBF73"));
    QCOMPARE(storedAccentChoice(4), QStringLiteral("#FFE8A33D"));
    QCOMPARE(storedAccentChoice(-1), QStringLiteral("#FF0097B2"));
    QCOMPARE(storedAccentChoice(5), QStringLiteral("#FF0097B2"));
}

void AppearanceSettingsTest::accentRoundTrips()
{
    for (int index = 0; index < lmp::core::settings::AccentChoiceCount; ++index) {
        QCOMPARE(accentChoiceFromStored(storedAccentChoice(index)), index);
    }
}

void AppearanceSettingsTest::backdropReadsTheExactNames_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<BackdropChoice>("expected");
    QTest::newRow("Solid") << QStringLiteral("Solid") << BackdropChoice::Solid;
    QTest::newRow("Mica") << QStringLiteral("Mica") << BackdropChoice::Mica;
    QTest::newRow("Acrylic") << QStringLiteral("Acrylic") << BackdropChoice::Acrylic;
    QTest::newRow("missing") << QString() << BackdropChoice::Solid;
    QTest::newRow("lower case") << QStringLiteral("acrylic") << BackdropChoice::Solid;
    QTest::newRow("unknown") << QStringLiteral("Glass") << BackdropChoice::Solid;
}

void AppearanceSettingsTest::backdropReadsTheExactNames()
{
    QFETCH(QString, stored);
    QFETCH(BackdropChoice, expected);
    QCOMPARE(backdropChoiceFromStored(stored), expected);
}

void AppearanceSettingsTest::backdropWritesTheStoredNames()
{
    QCOMPARE(storedBackdropChoice(BackdropChoice::Solid), QStringLiteral("Solid"));
    QCOMPARE(storedBackdropChoice(BackdropChoice::Mica), QStringLiteral("Mica"));
    QCOMPARE(storedBackdropChoice(BackdropChoice::Acrylic), QStringLiteral("Acrylic"));
}

QTEST_GUILESS_MAIN(AppearanceSettingsTest)
#include "tst_appearance_settings.moc"
