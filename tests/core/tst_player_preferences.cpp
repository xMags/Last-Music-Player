#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "PlayerFixture.h"
#include "core/settings/SettingKeys.h"

using lmp::core::PlayerController;
using lmp::core::RepeatMode;
using lmp::core::Shuffle;
namespace keys = lmp::core::settings::keys;

namespace {

QJsonObject readSettingsFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

// The player controller's saved preferences: shuffle, repeat, autoplay and
// volume under the keys earlier versions use, the volume save delay, the
// launch volume rule, and mute, which is never saved.
class PlayerPreferencesTest : public QObject {
    Q_OBJECT

private slots:
    void preferencesUseTheLegacyKeysAndAreRestored();
    void volumeIsSavedOnce350MillisecondsAfterTheLastChange();
    void storedSilentVolumeBecomesTheDefaultAtLaunch();
    void muteIsNeverSaved();
};

void PlayerPreferencesTest::preferencesUseTheLegacyKeysAndAreRestored()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    {
        auto player = fixture.makeController();
        QCOMPARE(player->shuffle(), Shuffle::Off);
        QCOMPARE(player->repeatMode(), RepeatMode::Off);
        QVERIFY(player->autoplay()); // default on
        player->setShuffle(Shuffle::On);
        player->setRepeatMode(RepeatMode::One);
        player->setAutoplay(false);
    }
    const QJsonObject saved = readSettingsFile(fixture.settingsPath());
    QVERIFY(saved.value(keys::ShuffleEnabled).isBool());
    QCOMPARE(saved.value(keys::ShuffleEnabled).toBool(), true);
    QVERIFY(saved.value(keys::RepeatMode).isDouble());
    QCOMPARE(saved.value(keys::RepeatMode).toInt(), 2);
    QVERIFY(saved.value(keys::Autoplay).isBool());
    QCOMPARE(saved.value(keys::Autoplay).toBool(), false);

    lmp::core::SettingsStore reread(fixture.settingsPath());
    auto restored = std::make_unique<PlayerController>(
        lmp::core::PlayerControllerServices{fixture.audio, reread, fixture.tracks, fixture.resolver, fixture.clock},
        lmp::core::QueueEngine(), seededRandom(3));
    QCOMPARE(restored->shuffle(), Shuffle::On);
    QCOMPARE(restored->repeatMode(), RepeatMode::One);
    QVERIFY(!restored->autoplay());

    // A stored repeat mode outside 0 to 2 reads as off.
    reread.setInteger(keys::RepeatMode, 7);
    auto lenient = std::make_unique<PlayerController>(
        lmp::core::PlayerControllerServices{fixture.audio, reread, fixture.tracks, fixture.resolver, fixture.clock},
        lmp::core::QueueEngine(), seededRandom(3));
    QCOMPARE(lenient->repeatMode(), RepeatMode::Off);
}

void PlayerPreferencesTest::volumeIsSavedOnce350MillisecondsAfterTheLastChange()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy savedSpy(&fixture.settings, &lmp::core::SettingsStore::changed);

    player->setVolume(0.4);
    fixture.clock.advance(200);
    player->setVolume(0.25); // restarts the wait
    QCOMPARE(fixture.audio.volumes.last(), 0.25);
    QCOMPARE(player->volume(), 0.25);
    fixture.clock.advance(349);
    QCOMPARE(savedSpy.count(), 0);
    QVERIFY(!readSettingsFile(fixture.settingsPath()).contains(keys::Volume));

    fixture.clock.advance(1);
    QCOMPARE(savedSpy.count(), 1);
    QCOMPARE(savedSpy.first().first().toString(), QString(keys::Volume));
    QCOMPARE(readSettingsFile(fixture.settingsPath()).value(keys::Volume).toDouble(), 0.25);

    // Out of range is clamped to 0 to 1, as stored.
    player->setVolume(1.8);
    fixture.clock.advance(350);
    QCOMPARE(readSettingsFile(fixture.settingsPath()).value(keys::Volume).toDouble(), 1.0);

    // Shutdown saves a change still waiting.
    player->setVolume(0.6);
    player->shutdown();
    QCOMPARE(readSettingsFile(fixture.settingsPath()).value(keys::Volume).toDouble(), 0.6);
    QCOMPARE(savedSpy.count(), 3);
}

void PlayerPreferencesTest::storedSilentVolumeBecomesTheDefaultAtLaunch()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    fixture.settings.setNumber(keys::Volume, 0.0);
    auto player = fixture.makeController();
    QCOMPARE(player->volume(), 0.7);
    QCOMPARE(fixture.audio.volumes.last(), 0.7);
    QCOMPARE(readSettingsFile(fixture.settingsPath()).value(keys::Volume).toDouble(), 0.7);

    PlayerFixture audible;
    QVERIFY(audible.ok());
    audible.settings.setNumber(keys::Volume, 0.3);
    auto kept = audible.makeController();
    QCOMPARE(kept->volume(), 0.3);
    QCOMPARE(audible.audio.volumes.last(), 0.3);
}

void PlayerPreferencesTest::muteIsNeverSaved()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    fixture.settings.setNumber(keys::Volume, 0.5);
    auto player = fixture.makeController();
    QSignalSpy savedSpy(&fixture.settings, &lmp::core::SettingsStore::changed);

    player->setMuted(true);
    QVERIFY(player->muted());
    QCOMPARE(fixture.audio.volumes.last(), 0.0);
    QCOMPARE(player->volume(), 0.5);
    fixture.clock.advance(5000);
    player->shutdown();
    QCOMPARE(savedSpy.count(), 0);
    QCOMPARE(readSettingsFile(fixture.settingsPath()).value(keys::Volume).toDouble(), 0.5);

    player->setMuted(false);
    QCOMPARE(fixture.audio.volumes.last(), 0.5);

    // Changing the level unmutes.
    player->setMuted(true);
    player->setVolume(0.45);
    QVERIFY(!player->muted());
    QCOMPARE(fixture.audio.volumes.last(), 0.45);
}

QTEST_GUILESS_MAIN(PlayerPreferencesTest)

#include "tst_player_preferences.moc"
