#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "core/settings/PreferenceDefaults.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

namespace keys = lmp::core::settings::keys;

// "Reset to defaults" rewrites exactly the keys earlier versions reset, in
// their JSON types, and nothing else.
class PreferenceDefaultsTest : public QObject {
    Q_OBJECT

private slots:
    void writesTheEarlierDefaults();
    void leavesEveryOtherKey();
};

namespace {

QJsonObject readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

void PreferenceDefaultsTest::writesTheEarlierDefaults()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Settings.json"));
    lmp::core::SettingsStore settings(path);
    settings.setBoolean(keys::ShowAlbumArt, false);
    settings.setBoolean(keys::WindowsMediaControls, false);
    settings.setBoolean(keys::Gapless, false);
    settings.setBoolean(keys::Autoplay, false);
    settings.setBoolean(keys::DiscordPresence, true);
    settings.setInteger(keys::CloseBehavior, 1);
    settings.setString(keys::ScanFormats, QStringLiteral(".wav"));
    settings.setString(keys::Theme, QStringLiteral("Dark"));
    settings.setString(keys::AccentColor, QStringLiteral("#FF6B8EFE"));
    settings.setString(keys::OutputDeviceId, QStringLiteral("device"));
    settings.setNumber(keys::eqBand(3), 4.5);
    settings.setNumber(keys::EqPreamp, -6.0);

    lmp::core::resetPreferencesToDefaults(settings);

    const QJsonObject json = readFile(path);
    QCOMPARE(json.value(keys::ShowAlbumArt), QJsonValue(true));
    QCOMPARE(json.value(keys::WindowsMediaControls), QJsonValue(true));
    QCOMPARE(json.value(keys::Gapless), QJsonValue(true));
    QCOMPARE(json.value(keys::Autoplay), QJsonValue(true));
    QCOMPARE(json.value(keys::DiscordPresence), QJsonValue(false));
    QCOMPARE(json.value(keys::CloseBehavior), QJsonValue(0));
    QCOMPARE(json.value(keys::ScanFormats), QJsonValue(QStringLiteral(".mp3,.flac")));
    QCOMPARE(json.value(keys::Theme), QJsonValue(QStringLiteral("System")));
    QCOMPARE(json.value(keys::AccentColor), QJsonValue(QStringLiteral("#FF0097B2")));
    QCOMPARE(json.value(keys::OutputDeviceId), QJsonValue(QString()));
    for (int band = 0; band < keys::EqBandCount; ++band) {
        QCOMPARE(json.value(keys::eqBand(band)), QJsonValue(0.0));
    }
    QCOMPARE(json.value(keys::EqPreamp), QJsonValue(0.0));
}

void PreferenceDefaultsTest::leavesEveryOtherKey()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Settings.json"));
    lmp::core::SettingsStore settings(path);
    settings.setVolume(0.3);
    settings.setBoolean(keys::ShuffleEnabled, true);
    settings.setInteger(keys::RepeatMode, 2);
    settings.setString(keys::UserDisplayName, QStringLiteral("Sam"));
    settings.setString(keys::RemoteAccessMode, QStringLiteral("ApiKey"));
    settings.setString(keys::MusicLibraryPath, QStringLiteral("C:/Music"));
    settings.setBoolean(keys::DataSaver, true);
    settings.setString(keys::FullscreenPanel, QStringLiteral("queue"));

    lmp::core::resetPreferencesToDefaults(settings);

    const QJsonObject json = readFile(path);
    QCOMPARE(json.value(keys::Volume), QJsonValue(0.3));
    QCOMPARE(json.value(keys::ShuffleEnabled), QJsonValue(true));
    QCOMPARE(json.value(keys::RepeatMode), QJsonValue(2));
    QCOMPARE(json.value(keys::UserDisplayName), QJsonValue(QStringLiteral("Sam")));
    QCOMPARE(json.value(keys::RemoteAccessMode), QJsonValue(QStringLiteral("ApiKey")));
    QCOMPARE(json.value(keys::MusicLibraryPath), QJsonValue(QStringLiteral("C:/Music")));
    QCOMPARE(json.value(keys::DataSaver), QJsonValue(true));
    QCOMPARE(json.value(keys::FullscreenPanel), QJsonValue(QStringLiteral("queue")));
}

QTEST_GUILESS_MAIN(PreferenceDefaultsTest)
#include "tst_preference_defaults.moc"
