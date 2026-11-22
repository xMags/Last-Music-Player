#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <climits>
#include <memory>

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

using lmp::core::SettingsStore;
namespace keys = lmp::core::settings::keys;

namespace {

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

QJsonObject readObject(const QString& path)
{
    return QJsonDocument::fromJson(readFile(path)).object();
}

} // namespace

class SettingsStoreTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void roundTripKeepsJsonTypes();
    void lenientReads();
    void schemaVersionIsInsertedWhenMissing();
    void atomicSaveLeavesNoTemporaryFile();
    void corruptFileIsQuarantinedAndGuarded();
    void invalidUtf8ReadsAsEmptyWithoutQuarantine();
    void failedSaveRollsBackOnlyForTrySetAndRemove();
    void resetKeepsOnlySchemaVersionAndCleansUp();
    void volumeIsClamped();
    void changedIsEmitted();
    void legacyFixtureLoadsEveryKey();

private:
    QString settingsPath() const { return m_dir->filePath(QStringLiteral("Settings.json")); }

    std::unique_ptr<QTemporaryDir> m_dir;
};

void SettingsStoreTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
}

void SettingsStoreTest::roundTripKeepsJsonTypes()
{
    {
        SettingsStore store(settingsPath());
        store.setString(keys::Theme, QStringLiteral("Dark"));
        store.setBoolean(keys::Autoplay, false);
        store.setInteger(keys::RepeatMode, 2);
        store.setNumber(keys::eqBand(3), -4.5);
    }
    SettingsStore again(settingsPath());
    QCOMPARE(again.string(keys::Theme), QStringLiteral("Dark"));
    QCOMPARE(again.boolean(keys::Autoplay, true), false);
    QCOMPARE(again.integer(keys::RepeatMode), 2);
    QCOMPARE(again.number(keys::eqBand(3)), -4.5);

    const QJsonObject saved = readObject(settingsPath());
    QVERIFY(saved.value(keys::Theme).isString());
    QVERIFY(saved.value(keys::Autoplay).isBool());
    QVERIFY(saved.value(keys::RepeatMode).isDouble());
    QVERIFY(saved.value(QStringLiteral("EqBand3")).isDouble());
    QCOMPARE(saved.value(keys::SchemaVersion).toDouble(), 1.0);
    // UTF-8 without a byte order mark.
    QCOMPARE(readFile(settingsPath()).at(0), '{');
}

void SettingsStoreTest::lenientReads()
{
    writeFile(settingsPath(), R"({"b1":1,"b2":0,"b3":"true","b4":"True","b5":"1","b6":"yes",)"
                              R"("i1":"42abc","i2":" -7","i3":"x","i4":3.9,"i5":-3.9,"i6":"99999999999","i7":1e12,)"
                              R"("d1":"1.5x","d2":"abc","d3":"1e999","d4":2,"s1":5,"s2":true})");
    SettingsStore store(settingsPath());
    QCOMPARE(store.boolean("b1"), true);
    QCOMPARE(store.boolean("b2", true), false);
    QCOMPARE(store.boolean("b3"), true);
    QCOMPARE(store.boolean("b4"), true);
    QCOMPARE(store.boolean("b5"), true);
    // Any other text is false, not the fallback.
    QCOMPARE(store.boolean("b6", true), false);
    QCOMPARE(store.boolean("missing", true), true);
    QCOMPARE(store.boolean("s1", false), true);

    QCOMPARE(store.integer("i1"), 42);
    QCOMPARE(store.integer("i2"), -7);
    QCOMPARE(store.integer("i3", 11), 11);
    QCOMPARE(store.integer("i4"), 3);
    QCOMPARE(store.integer("i5"), -3);
    QCOMPARE(store.integer("i6", 11), 11);
    QCOMPARE(store.integer("i7", 11), 11);
    QCOMPARE(store.integer("s2", 11), 11);

    QCOMPARE(store.number("d1"), 1.5);
    QCOMPARE(store.number("d2", 0.25), 0.25);
    QCOMPARE(store.number("d3", 0.25), 0.25);
    QCOMPARE(store.number("d4"), 2.0);

    // Only real strings are strings.
    QCOMPARE(store.string("s1", QStringLiteral("fallback")), QStringLiteral("fallback"));
    QCOMPARE(store.string("b3"), QStringLiteral("true"));
}

void SettingsStoreTest::schemaVersionIsInsertedWhenMissing()
{
    writeFile(settingsPath(), R"({"Theme":"Light"})");
    SettingsStore store(settingsPath());
    QCOMPARE(store.integer(keys::SchemaVersion), 1);
    store.save();
    QCOMPARE(readObject(settingsPath()).value(keys::SchemaVersion).toDouble(), 1.0);
    QCOMPARE(readObject(settingsPath()).value(keys::Theme).toString(), QStringLiteral("Light"));
}

void SettingsStoreTest::atomicSaveLeavesNoTemporaryFile()
{
    SettingsStore store(settingsPath());
    for (int i = 0; i < 5; ++i) {
        store.setInteger(keys::CloseBehavior, i % 2);
    }
    const QStringList names = QDir(m_dir->path()).entryList(QDir::Files | QDir::Hidden);
    QCOMPARE(names, QStringList{QStringLiteral("Settings.json")});
}

void SettingsStoreTest::corruptFileIsQuarantinedAndGuarded()
{
    writeFile(settingsPath(), "{not json");
    SettingsStore store(settingsPath());
    QCOMPARE(store.integer(keys::SchemaVersion), 1);

    const QStringList names = QDir(m_dir->path()).entryList(QDir::Files);
    QCOMPARE(names.size(), 1);
    static const QRegularExpression pattern(QStringLiteral("^Settings\\.json\\.corrupt-\\d{8}T\\d{6}$"));
    QVERIFY2(pattern.match(names.first()).hasMatch(), qPrintable(names.first()));
    QCOMPARE(readFile(m_dir->filePath(names.first())), QByteArray("{not json"));

    // Nothing is written until a real setting exists.
    store.save();
    QVERIFY(!QFile::exists(settingsPath()));
    store.setBoolean(keys::Gapless, true);
    QVERIFY(QFile::exists(settingsPath()));
    QCOMPARE(readObject(settingsPath()).value(keys::Gapless).toBool(), true);

    // A JSON value that is not an object is just as unreadable.
    QFile::remove(settingsPath());
    writeFile(settingsPath(), "[1,2]");
    SettingsStore other(settingsPath());
    QVERIFY(!other.contains(keys::Gapless));
    QVERIFY(!QFile::exists(settingsPath()));
}

void SettingsStoreTest::invalidUtf8ReadsAsEmptyWithoutQuarantine()
{
    writeFile(settingsPath(), QByteArray("{\"Theme\":\"\xFF\"}"));
    SettingsStore store(settingsPath());
    QVERIFY(!store.contains(keys::Theme));
    QCOMPARE(QDir(m_dir->path()).entryList(QDir::Files), QStringList{QStringLiteral("Settings.json")});
    // No write guard here: the next save replaces the unreadable file.
    store.save();
    QVERIFY(readObject(settingsPath()).contains(keys::SchemaVersion));
}

void SettingsStoreTest::failedSaveRollsBackOnlyForTrySetAndRemove()
{
    // The parent "folder" is a file, so the save cannot succeed.
    const QString blocker = m_dir->filePath(QStringLiteral("blocker"));
    writeFile(blocker, "x");
    SettingsStore store(blocker + QStringLiteral("/Settings.json"));

    QVERIFY(!store.trySetString(keys::Theme, QStringLiteral("Dark")));
    QVERIFY(!store.contains(keys::Theme));

    store.setBoolean(keys::Autoplay, false);
    QCOMPARE(store.boolean(keys::Autoplay, true), false);

    QVERIFY(!store.remove(keys::Autoplay));
    QVERIFY(store.contains(keys::Autoplay));
    // Removing a key that is not there succeeds without saving.
    QVERIFY(store.remove(keys::Theme));
}

void SettingsStoreTest::resetKeepsOnlySchemaVersionAndCleansUp()
{
    writeFile(settingsPath(), R"({"Theme":"Dark","Volume":0.4})");
    writeFile(settingsPath() + QStringLiteral(".corrupt-20250101T000000"), "old");
    writeFile(settingsPath() + QStringLiteral(".tmp"), "partial");
    writeFile(m_dir->filePath(QStringLiteral("Other.json")), "{}");

    SettingsStore store(settingsPath());
    store.setVolume(0.2);
    QVERIFY(store.reset());
    QVERIFY(!store.contains(keys::Theme));
    QCOMPARE(store.volume(), SettingsStore::DefaultVolume);

    const QJsonObject saved = readObject(settingsPath());
    QCOMPARE(saved.keys(), QStringList{QStringLiteral("SchemaVersion")});
    QStringList names = QDir(m_dir->path()).entryList(QDir::Files);
    names.sort();
    QCOMPARE(names, (QStringList{QStringLiteral("Other.json"), QStringLiteral("Settings.json")}));
}

void SettingsStoreTest::volumeIsClamped()
{
    SettingsStore store(settingsPath());
    QCOMPARE(store.volume(), 0.7);
    store.setVolume(1.5);
    QCOMPARE(store.volume(), 1.0);
    store.setVolume(-1);
    QCOMPARE(store.volume(), 0.0);
    store.setVolume(0.35);
    QCOMPARE(readObject(settingsPath()).value(keys::Volume).toDouble(), 0.35);
}

void SettingsStoreTest::changedIsEmitted()
{
    SettingsStore store(settingsPath());
    QSignalSpy spy(&store, &SettingsStore::changed);
    store.setString(keys::Theme, QStringLiteral("Light"));
    store.setInteger(keys::RepeatMode, 1);
    QVERIFY(store.remove(keys::Theme));
    QVERIFY(store.reset());
    QCOMPARE(spy.count(), 4);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Theme"));
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("RepeatMode"));
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("Theme"));
    QVERIFY(spy.at(3).at(0).toString().isEmpty());
}

void SettingsStoreTest::legacyFixtureLoadsEveryKey()
{
    // Written the way the earlier versions wrote the file: one compact object,
    // typed values, made-up contents.
    writeFile(settingsPath(),
              R"({"SchemaVersion":1,"Volume":0.55,"Theme":"Dark","AccentColor":"#FF6A5CD6",)"
              R"("Gapless":true,"DataSaver":true,"Autoplay":false,"ShuffleEnabled":true,"RepeatMode":1,)"
              R"("ShowAlbumArt":false,"WindowsMediaControls":true,"DiscordPresence":true,"CloseBehavior":1,)"
              R"("OutputDeviceId":"{0.0.0.00000000}.{test-device}","OutputDeviceIndex":2,)"
              R"("EqBand0":1.5,"EqBand1":-2,"EqBand2":0,"EqBand3":3,"EqBand4":-1.5,"EqBand5":0.5,)"
              R"("EqBand6":0,"EqBand7":-6,"EqBand8":2.5,"EqBand9":12,"EqPreamp":-3,)"
              R"("ScanFormats":".mp3,.flac,.ogg","MusicLibraryPath":"D:\\Music\\Test",)"
              R"("UserDisplayName":"Test Listener","WindowWidth":1600,"WindowHeight":900,)"
              R"("WindowX":-8,"WindowY":12,"RemoteAccessMode":"ApiKey",)"
              R"("ProviderBaseUrl":"http://127.0.0.1:4527","ProviderApiKey":"made-up-key-123",)"
              R"("CatalogStorefront":"gb","RecentSearches":"first\nsecond\\\\line",)"
              R"("BrowseResultView":"list","LibraryDetailDensity":"Compact",)"
              R"("LegacyHomeHistoryMigrated":true,"AccountOwnerHint":"owner-0001",)"
              R"("AccountDisplayNameHint":"Test Person","AccountUsernameHint":"tester",)"
              R"("AccountPlanHint":"Free","PendingAccountCleanupOwnerId":"owner-0002",)"
              R"("DownloadFolder":"E:\\Offline","DownloadOnlyOnWifi":false,"AutoDownloadLiked":false,)"
              R"("DownloadOnBattery":true,"KeepRecentOffline":false})");

    SettingsStore store(settingsPath());
    QCOMPARE(store.integer(keys::SchemaVersion), 1);
    QCOMPARE(store.volume(), 0.55);
    QCOMPARE(store.string(keys::Theme), QStringLiteral("Dark"));
    QCOMPARE(store.string(keys::AccentColor), QStringLiteral("#FF6A5CD6"));
    QCOMPARE(store.boolean(keys::Gapless), true);
    QCOMPARE(store.boolean(keys::DataSaver), true);
    QCOMPARE(store.boolean(keys::Autoplay, true), false);
    QCOMPARE(store.boolean(keys::ShuffleEnabled), true);
    QCOMPARE(store.integer(keys::RepeatMode), 1);
    QCOMPARE(store.boolean(keys::ShowAlbumArt, true), false);
    QCOMPARE(store.boolean(keys::WindowsMediaControls), true);
    QCOMPARE(store.boolean(keys::DiscordPresence), true);
    QCOMPARE(store.integer(keys::CloseBehavior), 1);
    QCOMPARE(store.string(keys::OutputDeviceId), QStringLiteral("{0.0.0.00000000}.{test-device}"));
    QCOMPARE(store.integer(keys::OutputDeviceIndex), 2);
    const double bands[keys::EqBandCount] = {1.5, -2, 0, 3, -1.5, 0.5, 0, -6, 2.5, 12};
    for (int band = 0; band < keys::EqBandCount; ++band) {
        QCOMPARE(store.number(keys::eqBand(band), 99), bands[band]);
    }
    QCOMPARE(store.number(keys::EqPreamp), -3.0);
    QCOMPARE(store.string(keys::ScanFormats), QStringLiteral(".mp3,.flac,.ogg"));
    QCOMPARE(store.string(keys::MusicLibraryPath), QStringLiteral("D:\\Music\\Test"));
    QCOMPARE(store.string(keys::UserDisplayName), QStringLiteral("Test Listener"));
    QCOMPARE(store.integer(keys::WindowWidth), 1600);
    QCOMPARE(store.integer(keys::WindowHeight), 900);
    QCOMPARE(store.integer(keys::WindowX, INT_MIN), -8);
    QCOMPARE(store.integer(keys::WindowY, INT_MIN), 12);
    QCOMPARE(store.string(keys::RemoteAccessMode), QStringLiteral("ApiKey"));
    QCOMPARE(store.string(keys::ProviderBaseUrl), QStringLiteral("http://127.0.0.1:4527"));
    QCOMPARE(store.string(keys::ProviderApiKey), QStringLiteral("made-up-key-123"));
    QCOMPARE(store.string(keys::CatalogStorefront), QStringLiteral("gb"));
    QCOMPARE(store.string(keys::RecentSearches), QStringLiteral("first\nsecond\\\\line"));
    QCOMPARE(store.string(keys::BrowseResultView), QStringLiteral("list"));
    QCOMPARE(store.string(keys::LibraryDetailDensity), QStringLiteral("Compact"));
    QCOMPARE(store.boolean(keys::LegacyHomeHistoryMigrated), true);
    QCOMPARE(store.string(keys::AccountOwnerHint), QStringLiteral("owner-0001"));
    QCOMPARE(store.string(keys::AccountDisplayNameHint), QStringLiteral("Test Person"));
    QCOMPARE(store.string(keys::AccountUsernameHint), QStringLiteral("tester"));
    QCOMPARE(store.string(keys::AccountPlanHint), QStringLiteral("Free"));
    QCOMPARE(store.string(keys::PendingAccountCleanupOwnerId), QStringLiteral("owner-0002"));
    QCOMPARE(store.string(keys::DownloadFolder), QStringLiteral("E:\\Offline"));
    QCOMPARE(store.boolean(keys::DownloadOnlyOnWifi, true), false);
    QCOMPARE(store.boolean(keys::AutoDownloadLiked, true), false);
    QCOMPARE(store.boolean(keys::DownloadOnBattery), true);
    QCOMPARE(store.boolean(keys::KeepRecentOffline, true), false);

    // Saving it again keeps every key with its type.
    store.setString(keys::Theme, QStringLiteral("Light"));
    const QJsonObject saved = readObject(settingsPath());
    QCOMPARE(saved.size(), 51);
    QVERIFY(saved.value(keys::WindowX).isDouble());
    QVERIFY(saved.value(keys::KeepRecentOffline).isBool());
}

QTEST_GUILESS_MAIN(SettingsStoreTest)
#include "tst_settings_store.moc"
