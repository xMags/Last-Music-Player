#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "ui/theme/ThemeColors.h"
#include "ui/theme/ThemeSettingsBinding.h"

using lmp::ui::ThemeColors;
using lmp::ui::ThemeSettingsBinding;
namespace keys = lmp::core::settings::keys;

// The saved theme and accent reach the theme at start-up and after every
// change, however the settings were changed.
class ThemeSettingsBindingTest : public QObject {
    Q_OBJECT

private slots:
    void appliesTheSavedChoiceWhenAttached();
    void followsLaterChanges();
    void followsAResetOfTheWholeFile();
    void detachedThemeIsLeftAlone();
};

void ThemeSettingsBindingTest::appliesTheSavedChoiceWhenAttached()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    lmp::core::SettingsStore settings(dir.filePath(QStringLiteral("Settings.json")));
    settings.setString(keys::Theme, QStringLiteral("Dark"));
    settings.setString(keys::AccentColor, QStringLiteral("#FFE0719C"));

    ThemeColors theme;
    ThemeSettingsBinding binding(settings);
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::System);
    binding.attach(&theme);
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::Dark);
    QCOMPARE(theme.accent(), ThemeColors::Accent::Coral);
}

void ThemeSettingsBindingTest::followsLaterChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    lmp::core::SettingsStore settings(dir.filePath(QStringLiteral("Settings.json")));
    ThemeColors theme;
    ThemeSettingsBinding binding(settings);
    binding.attach(&theme);
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::System);
    QCOMPARE(theme.accent(), ThemeColors::Accent::Teal);

    settings.setString(keys::Theme, QStringLiteral("Light"));
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::Light);
    settings.setString(keys::AccentColor, QStringLiteral("#FFE8A33D"));
    QCOMPARE(theme.accent(), ThemeColors::Accent::Amber);
    settings.setString(keys::AccentColor, QStringLiteral("#6A5CD6"));
    QCOMPARE(theme.accent(), ThemeColors::Accent::Purple);
    // An unreadable value falls back as earlier versions read it.
    settings.setString(keys::Theme, QStringLiteral("dark"));
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::System);
}

void ThemeSettingsBindingTest::followsAResetOfTheWholeFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    lmp::core::SettingsStore settings(dir.filePath(QStringLiteral("Settings.json")));
    settings.setString(keys::Theme, QStringLiteral("Dark"));
    settings.setString(keys::AccentColor, QStringLiteral("#FF6FBF73"));
    ThemeColors theme;
    ThemeSettingsBinding binding(settings);
    binding.attach(&theme);
    QCOMPARE(theme.accent(), ThemeColors::Accent::Green);

    QVERIFY(settings.reset());
    QCOMPARE(theme.mode(), ThemeColors::ThemeMode::System);
    QCOMPARE(theme.accent(), ThemeColors::Accent::Teal);
}

void ThemeSettingsBindingTest::detachedThemeIsLeftAlone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    lmp::core::SettingsStore settings(dir.filePath(QStringLiteral("Settings.json")));
    ThemeSettingsBinding binding(settings);
    {
        ThemeColors theme;
        binding.attach(&theme);
    }
    // The theme is gone; a change must not reach it.
    settings.setString(keys::Theme, QStringLiteral("Dark"));
    QCOMPARE(ThemeSettingsBinding::modeFor(QStringLiteral("Dark")), ThemeColors::ThemeMode::Dark);
}

QTEST_MAIN(ThemeSettingsBindingTest)
#include "tst_theme_settings_binding.moc"
