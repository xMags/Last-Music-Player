#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

#include <optional>

#include "ModeServiceFixture.h"
#include "core/account/AccountSessionService.h"
#include "core/account/SecretStore.h"
#include "core/async/Result.h"
#include "core/mode/ModeService.h"
#include "core/settings/SettingKeys.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::account::AccountOperationContext;
using lmp::core::account::LegacyKeyMigration;
using lmp::core::account::SecretStore;
using lmp::core::mode::Connectivity;
using lmp::core::mode::ModeService;
using lmp::core::mode::RemoteScope;
using lmp::core::mode::SourceMode;
using lmp::test::FakeHttpTransport;
using lmp::test::kProviderBase;
using lmp::test::kProviderStatusPath;
using lmp::test::ModeServiceFixture;
namespace keys = lmp::core::settings::keys;

namespace {

const QByteArray kBothPages = R"({"capabilities":["artist-detail-v1","album-detail-v1"]})";

} // namespace

// Mode rules, fallbacks, the database context and the remote scope. The API
// key connection has its own file (tst_mode_service_api_key.cpp).
class ModeServiceTest : public QObject {
    Q_OBJECT

private slots:
    void firstRunWithoutProviderIsLocalOnly();
    void firstRunWithProviderIsApiKey();
    void firstRunNeedsAddressAndKey_data();
    void firstRunNeedsAddressAndKey();
    void firstRunUsesTheMovedPlaintextKey();
    void storedValueIsRead_data();
    void storedValueIsRead();
    void nonTextValueCountsAsFirstRun();
    void startSetsTheDatabaseContext_data();
    void startSetsTheDatabaseContext();
    void availabilityFollowsTheSources();
    void accountIsNotSelectableWithoutSession();
    void enteringAccountUsesTheOwnersLibrary();
    void enteringApiKeyUnconfiguredIsAllowed();
    void refusedAccountContextFallsBack();
    void signOutFallsBackToLocalOnly();
    void rejectedSessionFallsBackToLocalOnly();
    void failedRestoreFallsBackToLocalOnly();
    void offlineRestoreKeepsAccount();
    void scopeStartsAtOneOnEveryStart();
    void scopeIsStaleAfterModeChanges_data();
    void scopeIsStaleAfterModeChanges();
    void scopeIsStaleAfterOwnerAndSessionChanges();
    void sessionChangesOutsideAccountKeepTheScope();
    void capabilitiesFollowModeAndConnectivity();
    void refreshReadsTheProviderCapabilities();
    void refreshResultIsDroppedAfterAModeChange();
    void refreshOutsideApiKeyIsNotAvailable();
};

void ModeServiceTest::firstRunWithoutProviderIsLocalOnly()
{
    ModeServiceFixture fixture;
    QCOMPARE(fixture.modes.start(), LegacyKeyMigration::NotNeeded);
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
}

void ModeServiceTest::firstRunWithProviderIsApiKey()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.storedMode(), QStringLiteral("ApiKey"));
    QVERIFY(fixture.modes.hasRemoteAccess());
}

void ModeServiceTest::firstRunNeedsAddressAndKey_data()
{
    QTest::addColumn<bool>("address");
    QTest::addColumn<bool>("key");
    QTest::newRow("address only") << true << false;
    QTest::newRow("key only") << false << true;
}

void ModeServiceTest::firstRunNeedsAddressAndKey()
{
    QFETCH(bool, address);
    QFETCH(bool, key);
    ModeServiceFixture fixture;
    if (address) {
        fixture.session.settings.setString(keys::ProviderBaseUrl, kProviderBase);
    }
    if (key) {
        fixture.session.credentials.values.insert(SecretStore::ProviderApiKeyTarget, QStringLiteral("key-a"));
    }
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
}

void ModeServiceTest::firstRunUsesTheMovedPlaintextKey()
{
    ModeServiceFixture fixture;
    fixture.session.settings.setString(keys::ProviderBaseUrl, kProviderBase);
    fixture.session.settings.setString(keys::ProviderApiKey, QStringLiteral("plain-key"));
    QCOMPARE(fixture.modes.start(), LegacyKeyMigration::Migrated);
    QCOMPARE(fixture.storedKey(), QStringLiteral("plain-key"));
    QVERIFY(!fixture.session.settings.contains(keys::ProviderApiKey));
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
}

void ModeServiceTest::storedValueIsRead_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<SourceMode>("mode");
    QTest::newRow("LocalOnly") << QStringLiteral("LocalOnly") << SourceMode::LocalOnly;
    QTest::newRow("ApiKey") << QStringLiteral("ApiKey") << SourceMode::ApiKey;
    QTest::newRow("Account") << QStringLiteral("Account") << SourceMode::Account;
    QTest::newRow("lower case api key") << QStringLiteral("apikey") << SourceMode::LocalOnly;
    QTest::newRow("upper case account") << QStringLiteral("ACCOUNT") << SourceMode::LocalOnly;
    QTest::newRow("leading space") << QStringLiteral(" ApiKey") << SourceMode::LocalOnly;
    QTest::newRow("trailing space") << QStringLiteral("Account ") << SourceMode::LocalOnly;
    QTest::newRow("unknown name") << QStringLiteral("Offline") << SourceMode::LocalOnly;
    QTest::newRow("number text") << QStringLiteral("2") << SourceMode::LocalOnly;
}

void ModeServiceTest::storedValueIsRead()
{
    QFETCH(QString, stored);
    QFETCH(SourceMode, mode);
    ModeServiceFixture fixture;
    // A configured provider must not turn a stored value into ApiKey.
    fixture.storeProvider();
    fixture.session.settings.setString(keys::RemoteAccessMode, stored);
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), mode);
    // Reading never rewrites the stored value.
    QCOMPARE(fixture.storedMode(), stored);
}

void ModeServiceTest::nonTextValueCountsAsFirstRun()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.session.settings.setInteger(keys::RemoteAccessMode, 1);
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.storedMode(), QStringLiteral("ApiKey"));
}

void ModeServiceTest::startSetsTheDatabaseContext_data()
{
    QTest::addColumn<QString>("stored");
    QTest::addColumn<QString>("context");
    QTest::newRow("LocalOnly") << QStringLiteral("LocalOnly") << QStringLiteral("LocalOnly");
    QTest::newRow("ApiKey") << QStringLiteral("ApiKey") << QStringLiteral("ApiKey");
    // The account context waits for the session to name its owner.
    QTest::newRow("Account") << QStringLiteral("Account") << QStringLiteral("LocalOnly");
}

void ModeServiceTest::startSetsTheDatabaseContext()
{
    QFETCH(QString, stored);
    QFETCH(QString, context);
    ModeServiceFixture fixture;
    QVERIFY(fixture.library.ok());
    QVERIFY(fixture.accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("old-owner")).isOk());
    fixture.session.settings.setString(keys::RemoteAccessMode, stored);
    fixture.modes.start();
    QCOMPARE(fixture.contextMode(), context);
    QCOMPARE(fixture.contextAccount(), QString());
}

void ModeServiceTest::availabilityFollowsTheSources()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.modes.isAvailable(SourceMode::LocalOnly));
    QVERIFY(!fixture.modes.isAvailable(SourceMode::ApiKey));
    QVERIFY(fixture.modes.isSelectable(SourceMode::ApiKey));
    QVERIFY(!fixture.modes.isAvailable(SourceMode::Account));
    QVERIFY(!fixture.modes.isSelectable(SourceMode::Account));
    QVERIFY(!fixture.modes.hasRemoteAccess());

    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.isAvailable(SourceMode::Account));
    QVERIFY(fixture.modes.isSelectable(SourceMode::Account));
    // Available is not the same as current.
    QVERIFY(!fixture.modes.hasRemoteAccess());
}

void ModeServiceTest::accountIsNotSelectableWithoutSession()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    const RemoteScope before = fixture.modes.captureScope();
    QSignalSpy changes(&fixture.modes, &ModeService::changed);
    QVERIFY(!fixture.modes.setMode(SourceMode::Account));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
    QVERIFY(fixture.modes.isCurrent(before));
    QCOMPARE(changes.size(), 0);
}

void ModeServiceTest::enteringAccountUsesTheOwnersLibrary()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QSignalSpy changes(&fixture.modes, &ModeService::changed);
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    QCOMPARE(fixture.modes.mode(), SourceMode::Account);
    QCOMPARE(fixture.storedMode(), QStringLiteral("Account"));
    QCOMPARE(fixture.contextMode(), QStringLiteral("Account"));
    QCOMPARE(fixture.contextAccount(), QStringLiteral("owner-a"));
    QVERIFY(fixture.modes.hasRemoteAccess());
    QCOMPARE(changes.size(), 1);

    QVERIFY(fixture.modes.setMode(SourceMode::LocalOnly));
    QCOMPARE(fixture.contextMode(), QStringLiteral("LocalOnly"));
    QCOMPARE(fixture.contextAccount(), QString());
}

void ModeServiceTest::enteringApiKeyUnconfiguredIsAllowed()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.modes.setMode(SourceMode::ApiKey));
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.storedMode(), QStringLiteral("ApiKey"));
    QCOMPARE(fixture.contextMode(), QStringLiteral("ApiKey"));
    QVERIFY(!fixture.modes.hasRemoteAccess());
}

void ModeServiceTest::refusedAccountContextFallsBack()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    fixture.library.database().close();
    QVERIFY(!fixture.modes.setMode(SourceMode::Account));
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
}

void ModeServiceTest::signOutFallsBackToLocalOnly()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    QVERIFY(fixture.session.service.removeLocalSession());
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
    QCOMPARE(fixture.contextMode(), QStringLiteral("LocalOnly"));
    QCOMPARE(fixture.contextAccount(), QString());
}

void ModeServiceTest::rejectedSessionFallsBackToLocalOnly()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    const std::optional<AccountOperationContext> context = fixture.session.service.captureOperation();
    QVERIFY(context.has_value());
    fixture.session.service.handleUnauthorized(*context);
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.contextMode(), QStringLiteral("LocalOnly"));
}

void ModeServiceTest::failedRestoreFallsBackToLocalOnly()
{
    ModeServiceFixture fixture;
    fixture.session.settings.setString(keys::RemoteAccessMode, QStringLiteral("Account"));
    fixture.modes.start();
    QCOMPARE(fixture.modes.mode(), SourceMode::Account);
    // No stored session: the restore signs out.
    fixture.session.service.restore();
    QCOMPARE(fixture.modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(fixture.storedMode(), QStringLiteral("LocalOnly"));
}

void ModeServiceTest::offlineRestoreKeepsAccount()
{
    ModeServiceFixture fixture;
    fixture.session.settings.setString(keys::RemoteAccessMode, QStringLiteral("Account"));
    fixture.modes.start();
    QVERIFY(fixture.restoreOffline(QStringLiteral("owner-b")));
    QCOMPARE(fixture.modes.mode(), SourceMode::Account);
    QVERIFY(fixture.modes.hasRemoteAccess());
    QCOMPARE(fixture.contextMode(), QStringLiteral("Account"));
    QCOMPARE(fixture.contextAccount(), QStringLiteral("owner-b"));
}

void ModeServiceTest::scopeStartsAtOneOnEveryStart()
{
    ModeServiceFixture first;
    first.storeProvider();
    first.modes.start();
    const RemoteScope scope = first.modes.captureScope();
    QCOMPARE(scope.mode, SourceMode::ApiKey);
    QCOMPARE(scope.generation, quint64{1});
    QVERIFY(first.modes.isCurrent(scope));

    ModeServiceFixture restarted;
    restarted.storeProvider();
    restarted.modes.start();
    QCOMPARE(restarted.modes.captureScope().generation, scope.generation);
}

void ModeServiceTest::scopeIsStaleAfterModeChanges_data()
{
    QTest::addColumn<QList<SourceMode>>("changes");
    QTest::newRow("to LocalOnly") << QList<SourceMode>{SourceMode::LocalOnly};
    QTest::newRow("to ApiKey") << QList<SourceMode>{SourceMode::ApiKey};
    QTest::newRow("to the same mode") << QList<SourceMode>{SourceMode::Account};
    QTest::newRow("away and back") << QList<SourceMode>{SourceMode::LocalOnly, SourceMode::Account};
}

void ModeServiceTest::scopeIsStaleAfterModeChanges()
{
    QFETCH(QList<SourceMode>, changes);
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    const RemoteScope scope = fixture.modes.captureScope();
    for (const SourceMode mode : changes) {
        QVERIFY(fixture.modes.setMode(mode));
    }
    QVERIFY(!fixture.modes.isCurrent(scope));
    QVERIFY(fixture.modes.isCurrent(fixture.modes.captureScope()));
}

void ModeServiceTest::scopeIsStaleAfterOwnerAndSessionChanges()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    const RemoteScope beforeReplacement = fixture.modes.captureScope();
    // Another sign-in on the same mode replaces the owner.
    QVERIFY(fixture.validate(QStringLiteral("owner-b")));
    QCOMPARE(fixture.modes.mode(), SourceMode::Account);
    QVERIFY(!fixture.modes.isCurrent(beforeReplacement));
    QCOMPARE(fixture.contextAccount(), QStringLiteral("owner-b"));

    const RemoteScope beforeSignOut = fixture.modes.captureScope();
    QVERIFY(fixture.session.service.removeLocalSession());
    QVERIFY(!fixture.modes.isCurrent(beforeSignOut));
}

void ModeServiceTest::sessionChangesOutsideAccountKeepTheScope()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    const RemoteScope scope = fixture.modes.captureScope();
    QSignalSpy changes(&fixture.modes, &ModeService::changed);
    // Same owner, new session state: API key work is not affected.
    QVERIFY(fixture.restoreOffline(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.isCurrent(scope));
    QVERIFY(changes.size() >= 1);
    QCOMPARE(fixture.modes.mode(), SourceMode::ApiKey);
    QCOMPARE(fixture.contextMode(), QStringLiteral("ApiKey"));
}

void ModeServiceTest::capabilitiesFollowModeAndConnectivity()
{
    ModeServiceFixture fixture;
    fixture.modes.start();
    QVERIFY(!fixture.modes.capabilities().searchOnline);
    QVERIFY(fixture.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.setMode(SourceMode::Account));
    QVERIFY(fixture.modes.capabilities().browseCatalogue);
    QVERIFY(fixture.modes.capabilities().streamRemoteSongs);
    QVERIFY(!fixture.modes.capabilities().lowQualityOnThisConnection);

    QSignalSpy changes(&fixture.modes, &ModeService::changed);
    fixture.modes.setConnectivity(Connectivity{false, true});
    QCOMPARE(changes.size(), 1);
    QVERIFY(!fixture.modes.capabilities().streamRemoteSongs);
    QVERIFY(fixture.modes.capabilities().lowQualityOnThisConnection);
    fixture.modes.setConnectivity(Connectivity{false, true});
    QCOMPARE(changes.size(), 1);
}

void ModeServiceTest::refreshReadsTheProviderCapabilities()
{
    ModeServiceFixture fixture;
    fixture.storeProvider(kProviderBase + QStringLiteral("/"));
    fixture.modes.start();
    QVERIFY(!fixture.modes.capabilities().openOnlineArtistPages);
    fixture.session.transport.script(kProviderStatusPath, FakeHttpTransport::ok(kBothPages));
    QSignalSpy changes(&fixture.modes, &ModeService::changed);
    const std::optional<Result<void>> refreshed = ModeServiceFixture::outcomeOf(fixture.modes.refreshProviderStatus());
    QVERIFY(refreshed.has_value() && refreshed->isOk());
    QCOMPARE(changes.size(), 1);
    QVERIFY(fixture.modes.capabilities().openOnlineArtistPages);
    QVERIFY(fixture.modes.capabilities().openOnlineAlbumPages);
    const auto& request = fixture.session.transport.requests.constLast();
    QCOMPARE(request.url.toString(), kProviderBase + kProviderStatusPath);
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer key-a"));
}

void ModeServiceTest::refreshResultIsDroppedAfterAModeChange()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.modes.start();
    QFuture<Result<void>> refresh = fixture.modes.refreshProviderStatus();
    QVERIFY(fixture.modes.setMode(SourceMode::LocalOnly));
    QVERIFY(fixture.modes.setMode(SourceMode::ApiKey));
    QVERIFY(fixture.session.transport.answer(kProviderStatusPath, FakeHttpTransport::ok(kBothPages)));
    const std::optional<Result<void>> refreshed = ModeServiceFixture::outcomeOf(refresh);
    QVERIFY(refreshed.has_value() && !refreshed->isOk());
    QCOMPARE(refreshed->failure().kind, FailureKind::Cancelled);
    QVERIFY(fixture.modes.providerCapabilities().isEmpty());
}

void ModeServiceTest::refreshOutsideApiKeyIsNotAvailable()
{
    ModeServiceFixture fixture;
    fixture.storeProvider();
    fixture.session.settings.setString(keys::RemoteAccessMode, QStringLiteral("LocalOnly"));
    fixture.modes.start();
    const std::optional<Result<void>> refreshed = ModeServiceFixture::outcomeOf(fixture.modes.refreshProviderStatus());
    QVERIFY(refreshed.has_value() && !refreshed->isOk());
    QCOMPARE(refreshed->failure().kind, FailureKind::NotAvailable);
    QCOMPARE(fixture.session.transport.requests.size(), 0);
}

QTEST_GUILESS_MAIN(ModeServiceTest)
#include "tst_mode_service.moc"
