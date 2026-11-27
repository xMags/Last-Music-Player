#include <QFuture>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <optional>

#include "ModeServiceFixture.h"
#include "core/account/SecretStore.h"
#include "core/mode/ModeService.h"
#include "core/settings/SettingKeys.h"

using lmp::core::FailureKind;
using lmp::core::account::SecretStore;
using lmp::core::mode::ApiKeyConnectOutcome;
using lmp::core::mode::ApiKeyDisconnectOutcome;
using lmp::core::mode::ModeService;
using lmp::core::mode::RemoteScope;
using lmp::core::mode::SourceMode;
using lmp::test::FakeHttpTransport;
using lmp::test::kProviderBase;
using lmp::test::kProviderStatusPath;
using lmp::test::ModeServiceFixture;
namespace keys = lmp::core::settings::keys;

namespace {

const QByteArray kArtistPages = R"({"capabilities":["artist-detail-v1"]})";

} // namespace

// Connecting and disconnecting the user's provider.
class ModeServiceApiKeyTest : public QObject {
    Q_OBJECT

private slots:
    void connectStoresTheKeyAndEntersApiKey();
    void connectFromAccountSwitchesToApiKey();
    void connectRefusesBeforeSending_data();
    void connectRefusesBeforeSending();
    void failedStatusCallStoresNothing_data();
    void failedStatusCallStoresNothing();
    void connectIsRefusedDuringCleanup();
    void keyThatCannotBeStoredDoesNotConnect_data();
    void keyThatCannotBeStoredDoesNotConnect();
    void newerConnectSupersedesOlder();
    void modeChangeSupersedesConnect();
    void disconnectFallsBackToLocalOnly();
    void theSavedProviderOutlivesALocalOnlySwitch();
    void disconnectOutsideApiKeyKeepsTheMode();
    void failedKeyRemovalChangesNothing();
    void disconnectIsRefusedDuringCleanup();
    void disconnectSupersedesConnect();
};

void ModeServiceApiKeyTest::connectStoresTheKeyAndEntersApiKey()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    const RemoteScope before = fixture.modes.captureScope();
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::ok(kArtistPages));
    QSignalSpy changes(&fixture.modes, &ModeService::changed);

    const QString typed = kProviderBase + QStringLiteral("/");
    QCOMPARE(fixture.connect(typed, QStringLiteral("key-a")), ApiKeyConnectOutcome::Connected);

    QCOMPARE(fixture.storedKey(), QStringLiteral("key-a"));
    QCOMPARE(fixture.session.credentials.lastWriteTarget, QStringLiteral("LastMusicPlayer/ProviderApiKey/v1"));
    QCOMPARE(fixture.session.settings.string(keys::ProviderBaseUrl), typed);
    QCOMPARE(fixture.modes.providerBaseUrl(), typed);
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.storedMode(), QStringLiteral("ApiKey"));
    QCOMPARE(fixture.contextMode(), QStringLiteral("ApiKey"));
    QVERIFY(fixture.modes.hasRemoteAccess());
    QVERIFY(!fixture.modes.isCurrent(before));
    QVERIFY(changes.size() >= 1);
    QCOMPARE(fixture.modes.providerCapabilities(), QStringList{QStringLiteral("artist-detail-v1")});
    QVERIFY(fixture.modes.capabilities().openOnlineArtistPages);
    QVERIFY(!fixture.modes.capabilities().openOnlineAlbumPages);

    QCOMPARE(fixture.session.transport.requests.size(), 1);
    const auto& request = fixture.session.transport.requests.constFirst();
    QCOMPARE(request.url.toString(), kProviderBase + kProviderStatusPath);
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer key-a"));
}

void ModeServiceApiKeyTest::connectFromAccountSwitchesToApiKey()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::ok("{}"));
    QCOMPARE(fixture.connect(kProviderBase, QStringLiteral("key-a")), ApiKeyConnectOutcome::Connected);
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.contextMode(), QStringLiteral("ApiKey"));
    QCOMPARE(fixture.contextAccount(), QString());
}

void ModeServiceApiKeyTest::connectRefusesBeforeSending_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("key");
    QTest::addColumn<ApiKeyConnectOutcome>("outcome");
    QTest::newRow("missing address") << QString() << QStringLiteral("key-a") << ApiKeyConnectOutcome::MissingAddress;
    QTest::newRow("missing key") << kProviderBase << QString() << ApiKeyConnectOutcome::MissingKey;
    QTest::newRow("missing both") << QString() << QString() << ApiKeyConnectOutcome::MissingAddress;
    QTest::newRow("no scheme") << QStringLiteral("provider.example.test") << QStringLiteral("key-a")
                               << ApiKeyConnectOutcome::InvalidAddress;
    QTest::newRow("other scheme") << QStringLiteral("ftp://provider.example.test") << QStringLiteral("key-a")
                                  << ApiKeyConnectOutcome::InvalidAddress;
    QTest::newRow("user info") << QStringLiteral("https://me:pw@provider.example.test") << QStringLiteral("key-a")
                               << ApiKeyConnectOutcome::InvalidAddress;
    QTest::newRow("query") << QStringLiteral("https://provider.example.test/?a=b") << QStringLiteral("key-a")
                           << ApiKeyConnectOutcome::InvalidAddress;
    QTest::newRow("line break in key") << kProviderBase << QStringLiteral("key\nX: 1")
                                       << ApiKeyConnectOutcome::InvalidKey;
}

void ModeServiceApiKeyTest::connectRefusesBeforeSending()
{
    QFETCH(QString, base);
    QFETCH(QString, key);
    QFETCH(ApiKeyConnectOutcome, outcome);
    ModeServiceFixture fixture;
    fixture.modes.start();
    QCOMPARE(fixture.connect(base, key), outcome);
    QCOMPARE(fixture.session.transport.requests.size(), 0);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
}

void ModeServiceApiKeyTest::failedStatusCallStoresNothing_data()
{
    QTest::addColumn<FailureKind>("kind");
    QTest::addColumn<int>("status");
    QTest::addColumn<ApiKeyConnectOutcome>("outcome");
    QTest::newRow("401") << FailureKind::Unauthorized << 401 << ApiKeyConnectOutcome::Unauthorized;
    QTest::newRow("403") << FailureKind::Forbidden << 403 << ApiKeyConnectOutcome::Unavailable;
    QTest::newRow("404") << FailureKind::NotFound << 404 << ApiKeyConnectOutcome::Unavailable;
    QTest::newRow("500") << FailureKind::Server << 500 << ApiKeyConnectOutcome::Unavailable;
    QTest::newRow("redirect") << FailureKind::InvalidResponse << 302 << ApiKeyConnectOutcome::Unavailable;
    QTest::newRow("no connection") << FailureKind::Network << 0 << ApiKeyConnectOutcome::Unavailable;
    QTest::newRow("timeout") << FailureKind::Timeout << 0 << ApiKeyConnectOutcome::Unavailable;
}

void ModeServiceApiKeyTest::failedStatusCallStoresNothing()
{
    QFETCH(FailureKind, kind);
    QFETCH(int, status);
    QFETCH(ApiKeyConnectOutcome, outcome);
    ModeServiceFixture fixture;
    fixture.modes.start();
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::failed(kind, status));
    QCOMPARE(fixture.connect(kProviderBase, QStringLiteral("key-a")), outcome);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QVERIFY(!fixture.session.settings.contains(keys::ProviderBaseUrl));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
}

void ModeServiceApiKeyTest::connectIsRefusedDuringCleanup()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::ok("{}"));
    QVERIFY(fixture.session.gate.closeAdmissions());
    QCOMPARE(fixture.connect(kProviderBase, QStringLiteral("key-a")), ApiKeyConnectOutcome::Busy);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    fixture.session.gate.reopen();
}

void ModeServiceApiKeyTest::keyThatCannotBeStoredDoesNotConnect_data()
{
    QTest::addColumn<bool>("failWrite");
    QTest::newRow("write refused") << true;
    QTest::newRow("read back differs") << false;
}

void ModeServiceApiKeyTest::keyThatCannotBeStoredDoesNotConnect()
{
    QFETCH(bool, failWrite);
    ModeServiceFixture fixture;
    fixture.modes.start();
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::ok("{}"));
    fixture.session.credentials.failWrite = failWrite;
    fixture.session.credentials.corruptNextRead = !failWrite;
    QCOMPARE(fixture.connect(kProviderBase, QStringLiteral("key-a")), ApiKeyConnectOutcome::StorageFailed);
    QVERIFY(!fixture.session.settings.contains(keys::ProviderBaseUrl));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QVERIFY(!fixture.modes.isAvailable(SourceMode::ApiKey));
}

void ModeServiceApiKeyTest::newerConnectSupersedesOlder()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QFuture<ApiKeyConnectOutcome> first = fixture.modes.connectApiKey(kProviderBase, QStringLiteral("key-a"));
    QFuture<ApiKeyConnectOutcome> second = fixture.modes.connectApiKey(kProviderBase, QStringLiteral("key-b"));
    QCOMPARE(fixture.session.transport.waitingCount(kProviderStatusPath), 2);
    QVERIFY(fixture.session.transport.answer(kProviderStatusPath, FakeHttpTransport::ok("{}")));
    QCOMPARE(ModeServiceFixture::connectOutcome(first), ApiKeyConnectOutcome::Superseded);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QVERIFY(fixture.session.transport.answer(kProviderStatusPath, FakeHttpTransport::ok("{}")));
    QCOMPARE(ModeServiceFixture::connectOutcome(second), ApiKeyConnectOutcome::Connected);
    QCOMPARE(fixture.storedKey(), QStringLiteral("key-b"));
}

void ModeServiceApiKeyTest::modeChangeSupersedesConnect()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QFuture<ApiKeyConnectOutcome> pending = fixture.modes.connectApiKey(kProviderBase, QStringLiteral("key-a"));
    QVERIFY(fixture.modes.setMode(SourceMode::LocalOnly));
    QVERIFY(fixture.session.transport.answer(kProviderStatusPath, FakeHttpTransport::ok("{}")));
    QCOMPARE(ModeServiceFixture::connectOutcome(pending), ApiKeyConnectOutcome::Superseded);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
}

void ModeServiceApiKeyTest::theSavedProviderOutlivesALocalOnlySwitch()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QVERIFY(fixture.modes.providerConnection().has_value());
    QVERIFY(fixture.modes.setMode(SourceMode::LocalOnly));
    // Nothing is sent outside API key mode, but its downloads can be found.
    QVERIFY(!fixture.modes.providerConnection().has_value());
    QVERIFY(fixture.modes.savedProviderConnection().has_value());
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::Disconnected);
    QVERIFY(!fixture.modes.savedProviderConnection().has_value());
}

void ModeServiceApiKeyTest::disconnectFallsBackToLocalOnly()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    const RemoteScope before = fixture.modes.captureScope();
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::Disconnected);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
    QCOMPARE(fixture.session.credentials.lastRemoveTarget, QString(SecretStore::ProviderApiKeyTarget));
    QVERIFY(!fixture.session.settings.contains(keys::ProviderBaseUrl));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
    QCOMPARE(fixture.contextMode(), QStringLiteral("LocalOnly"));
    QVERIFY(!fixture.modes.isCurrent(before));
    QVERIFY(!fixture.modes.isAvailable(SourceMode::ApiKey));
}

void ModeServiceApiKeyTest::disconnectOutsideApiKeyKeepsTheMode()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.session.settings.setString(keys::RemoteAccessMode, QStringLiteral("Account"));
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    const RemoteScope before = fixture.modes.captureScope();
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::Disconnected);
    QCOMPARE(fixture.modes.mode(), SourceMode::Account);
    QCOMPARE(fixture.contextAccount(), QStringLiteral("owner-a"));
    QVERIFY(!fixture.modes.isCurrent(before));
}

void ModeServiceApiKeyTest::failedKeyRemovalChangesNothing()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    fixture.session.credentials.failRemove = true;
    const RemoteScope before = fixture.modes.captureScope();
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::KeyRemovalFailed);
    QCOMPARE(fixture.storedKey(), QStringLiteral("key-a"));
    QVERIFY(fixture.session.settings.contains(keys::ProviderBaseUrl));
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QVERIFY(fixture.modes.isCurrent(before));
}

void ModeServiceApiKeyTest::disconnectIsRefusedDuringCleanup()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QVERIFY(fixture.session.gate.closeAdmissions());
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::Busy);
    QCOMPARE(fixture.storedKey(), QStringLiteral("key-a"));
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    fixture.session.gate.reopen();
}

void ModeServiceApiKeyTest::disconnectSupersedesConnect()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QFuture<ApiKeyConnectOutcome> pending = fixture.modes.connectApiKey(kProviderBase, QStringLiteral("key-a"));
    QCOMPARE(fixture.modes.disconnectApiKey(), ApiKeyDisconnectOutcome::Disconnected);
    QVERIFY(fixture.session.transport.answer(kProviderStatusPath, FakeHttpTransport::ok("{}")));
    QCOMPARE(ModeServiceFixture::connectOutcome(pending), ApiKeyConnectOutcome::Superseded);
    QCOMPARE(fixture.storedKey(), QStringLiteral("<none>"));
}

QTEST_GUILESS_MAIN(ModeServiceApiKeyTest)
#include "tst_mode_service_api_key.moc"
