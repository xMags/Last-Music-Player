#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/mode/SourceMode.h"
#include "core/settings/SettingKeys.h"
#include "ui/settings/SettingsViewModel.h"

using lmp::ui::SettingsViewModel;
namespace keys = lmp::core::settings::keys;

// Every Settings row reads and writes its key in Settings.json in the type
// and format installed earlier versions use.
class SettingsViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void rowsStartAtTheEarlierDefaults();
    void rowsWriteTheirKeysInTheEarlierFormats();
    void rowsFollowChangesMadeElsewhere();
    void closeBehaviorReadsLikeEarlierVersions();
    void unknownChoicesAreIgnored();
    void displayNameIsTrimmedOfAsciiSpaceOnly();
    void displayNameIsShownOutsideAccountMode();
    void resetRestoresTheDefaultsAndThePlayersAutoplay();

private:
    [[nodiscard]] QJsonObject savedFile() const;

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<SettingsViewModel> m_model;
};

void SettingsViewModelTest::init()
{
    m_player = std::make_unique<PlayerFixture>();
    QVERIFY(m_player->ok());
    m_modes = std::make_unique<lmp::test::ModeServiceFixture>();
    m_controller = m_player->makeController();
    m_model = std::make_unique<SettingsViewModel>(m_player->settings, *m_controller, m_modes->modes);
}

void SettingsViewModelTest::cleanup()
{
    m_model.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

QJsonObject SettingsViewModelTest::savedFile() const
{
    QFile file(m_player->settingsPath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void SettingsViewModelTest::rowsStartAtTheEarlierDefaults()
{
    QCOMPARE(m_model->theme(), QStringLiteral("System"));
    QCOMPARE(m_model->accentIndex(), 0);
    QVERIFY(m_model->autoplay());
    QVERIFY(m_model->gapless());
    QVERIFY(m_model->showAlbumArt());
    QVERIFY(m_model->mediaControls());
    QCOMPARE(m_model->closeBehavior(), 0);
    QCOMPARE(m_model->displayName(), QString());
    QCOMPARE(m_model->accentSeeds().size(), 5);
    QCOMPARE(m_model->accentNames().size(), 5);
    QCOMPARE(m_model->closeChoices().size(), 2);
}

void SettingsViewModelTest::rowsWriteTheirKeysInTheEarlierFormats()
{
    m_model->setTheme(QStringLiteral("Dark"));
    m_model->setAccentIndex(2);
    m_model->setAutoplay(false);
    m_model->setGapless(false);
    m_model->setShowAlbumArt(false);
    m_model->setMediaControls(false);
    m_model->setCloseBehavior(1);
    QCOMPARE(m_model->saveDisplayName(QStringLiteral("  Sam  ")), QStringLiteral("Sam"));

    const QJsonObject json = savedFile();
    QCOMPARE(json.value(keys::Theme), QJsonValue(QStringLiteral("Dark")));
    QCOMPARE(json.value(keys::AccentColor), QJsonValue(QStringLiteral("#FFE0719C")));
    QCOMPARE(json.value(keys::Autoplay), QJsonValue(false));
    QCOMPARE(json.value(keys::Gapless), QJsonValue(false));
    QCOMPARE(json.value(keys::ShowAlbumArt), QJsonValue(false));
    QCOMPARE(json.value(keys::WindowsMediaControls), QJsonValue(false));
    QVERIFY(json.value(keys::CloseBehavior).isDouble());
    QCOMPARE(json.value(keys::CloseBehavior).toInt(), 1);
    QCOMPARE(json.value(keys::UserDisplayName), QJsonValue(QStringLiteral("Sam")));

    // And back on: the switches store true, not a missing key.
    m_model->setGapless(true);
    m_model->setShowAlbumArt(true);
    m_model->setMediaControls(true);
    m_model->setCloseBehavior(0);
    const QJsonObject after = savedFile();
    QCOMPARE(after.value(keys::Gapless), QJsonValue(true));
    QCOMPARE(after.value(keys::ShowAlbumArt), QJsonValue(true));
    QCOMPARE(after.value(keys::WindowsMediaControls), QJsonValue(true));
    QCOMPARE(after.value(keys::CloseBehavior).toInt(), 0);
    QVERIFY(!m_controller->autoplay());
}

void SettingsViewModelTest::rowsFollowChangesMadeElsewhere()
{
    QSignalSpy changed(m_model.get(), &SettingsViewModel::preferencesChanged);
    m_player->settings.setString(keys::Theme, QStringLiteral("Light"));
    QVERIFY(changed.count() >= 1);
    QCOMPARE(m_model->theme(), QStringLiteral("Light"));

    m_player->settings.setString(keys::AccentColor, QStringLiteral("#FFE8A33D"));
    QCOMPARE(m_model->accentIndex(), 4);

    const qsizetype before = changed.count();
    m_controller->setAutoplay(false);
    QVERIFY(changed.count() > before);
    QVERIFY(!m_model->autoplay());
}

void SettingsViewModelTest::closeBehaviorReadsLikeEarlierVersions()
{
    // Only 0 closed to the tray; every other number quit.
    m_player->settings.setInteger(keys::CloseBehavior, 0);
    QCOMPARE(m_model->closeBehavior(), 0);
    m_player->settings.setInteger(keys::CloseBehavior, 7);
    QCOMPARE(m_model->closeBehavior(), 1);
    m_player->settings.setInteger(keys::CloseBehavior, -1);
    QCOMPARE(m_model->closeBehavior(), 1);
}

void SettingsViewModelTest::unknownChoicesAreIgnored()
{
    m_model->setTheme(QStringLiteral("Dark"));
    // An unknown theme stores System, the earlier versions' reading of it.
    m_model->setTheme(QStringLiteral("Sepia"));
    QCOMPARE(savedFile().value(keys::Theme), QJsonValue(QStringLiteral("System")));

    m_model->setAccentIndex(3);
    m_model->setAccentIndex(9);
    m_model->setAccentIndex(-1);
    QCOMPARE(savedFile().value(keys::AccentColor), QJsonValue(QStringLiteral("#FF6FBF73")));

    m_model->setCloseBehavior(1);
    m_model->setCloseBehavior(2);
    QCOMPARE(savedFile().value(keys::CloseBehavior).toInt(), 1);
}

void SettingsViewModelTest::displayNameIsTrimmedOfAsciiSpaceOnly()
{
    QCOMPARE(SettingsViewModel::trimmedName(QStringLiteral(" \t Sam Lee \r\n")), QStringLiteral("Sam Lee"));
    QCOMPARE(SettingsViewModel::trimmedName(QStringLiteral("   ")), QString());
    // The earlier trim used the C locale, which keeps a no-break space.
    const QString noBreak = QString(QChar(0x00A0)) + QStringLiteral("Sam");
    QCOMPARE(SettingsViewModel::trimmedName(noBreak), noBreak);

    QVERIFY(!m_model->isDisplayNameEdit(QStringLiteral("   ")));
    QVERIFY(m_model->isDisplayNameEdit(QStringLiteral("Sam")));
    QCOMPARE(m_model->saveDisplayName(QStringLiteral("Sam ")), QStringLiteral("Sam"));
    QVERIFY(!m_model->isDisplayNameEdit(QStringLiteral(" Sam")));
    QCOMPARE(m_model->displayName(), QStringLiteral("Sam"));
    // An empty name may be saved.
    QCOMPARE(m_model->saveDisplayName(QStringLiteral(" ")), QString());
    QCOMPARE(savedFile().value(keys::UserDisplayName), QJsonValue(QString()));
}

void SettingsViewModelTest::displayNameIsShownOutsideAccountMode()
{
    auto& modes = m_modes->modes;
    QCOMPARE(modes.mode(), lmp::core::mode::SourceMode::LocalOnly);
    QVERIFY(m_model->displayNameShown());

    m_modes->storeProvider();
    (void)modes.start();
    QVERIFY(modes.setMode(lmp::core::mode::SourceMode::ApiKey));
    QVERIFY(m_model->displayNameShown());

    QSignalSpy changed(m_model.get(), &SettingsViewModel::preferencesChanged);
    QVERIFY(m_modes->validate(QStringLiteral("owner-a")));
    QVERIFY(modes.setMode(lmp::core::mode::SourceMode::Account));
    QVERIFY(changed.count() >= 1);
    QVERIFY(!m_model->displayNameShown());
}

void SettingsViewModelTest::resetRestoresTheDefaultsAndThePlayersAutoplay()
{
    m_model->setTheme(QStringLiteral("Dark"));
    m_model->setAccentIndex(1);
    m_model->setAutoplay(false);
    m_model->setShowAlbumArt(false);
    m_model->setCloseBehavior(1);
    m_model->saveDisplayName(QStringLiteral("Sam"));

    m_model->resetDefaults();

    QCOMPARE(m_model->theme(), QStringLiteral("System"));
    QCOMPARE(m_model->accentIndex(), 0);
    QVERIFY(m_model->autoplay());
    QVERIFY(m_controller->autoplay());
    QVERIFY(m_model->showAlbumArt());
    QCOMPARE(m_model->closeBehavior(), 0);
    // Not part of the reset.
    QCOMPARE(m_model->displayName(), QStringLiteral("Sam"));
}

QTEST_MAIN(SettingsViewModelTest)
#include "tst_settings_view_model.moc"
