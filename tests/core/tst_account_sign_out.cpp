#include <QCoreApplication>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <functional>
#include <optional>

#include "AccountSessionFixture.h"
#include "core/account/AccountDataLifecycleService.h"
#include "core/account/AccountDataLifecycleStorage.h"
#include "core/settings/SettingKeys.h"

using lmp::core::Result;
using lmp::core::account::AccountCleanupOutcome;
using lmp::core::account::AccountCleanupResult;
using lmp::core::account::AccountDataLifecycleService;
using lmp::core::account::AccountLogoutOutcome;
using lmp::core::account::AccountLogoutResult;
using lmp::core::account::AccountSessionState;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
namespace keys = lmp::core::settings::keys;

namespace {

const QString kLogoutPath = QStringLiteral("/v1/auth/logout");

// Account data kept per owner, with what each could still send.
class FakeLifecycleStorage final : public lmp::core::account::AccountDataLifecycleStorage {
public:
    [[nodiscard]] bool isAccountDataStoreReady() const noexcept override { return ready; }
    [[nodiscard]] std::optional<std::size_t> recoverablePendingChangeCount(const QString& accountId) const override
    {
        return static_cast<std::size_t>(pending.value(accountId, 0));
    }
    [[nodiscard]] Result<void> removeSignedOutAccountData(const QString& accountId) override
    {
        if (failRemove) {
            return lmp::core::Failure::of(lmp::core::FailureKind::Storage);
        }
        owners.remove(accountId);
        pending.remove(accountId);
        return Result<void>::success();
    }

    bool ready = true;
    bool failRemove = false;
    QSet<QString> owners;
    QHash<QString, int> pending;
};

class FakeOutboundSync final : public lmp::core::account::AccountOutboundSync {
public:
    QFuture<bool> requestOutboundSync() override
    {
        ++requests;
        if (onRequest) {
            onRequest();
        }
        return QtFuture::makeReadyValueFuture(succeeds);
    }

    int requests = 0;
    bool succeeds = true;
    std::function<void()> onRequest;
};

struct LifecycleFixture {
    LifecycleFixture()
        : lifecycle({.session = session.service,
                     .outboundSync = sync,
                     .storage = storage,
                     .settings = session.settings,
                     .gate = session.gate,
                     .idleTimeout = std::chrono::seconds(2)})
    {
    }

    [[nodiscard]] bool signIn(const QString& bearer, const QString& owner)
    {
        session.storeSession(bearer);
        session.transport.script(lmp::test::kProfilePath, AccountSessionFixture::profileAnswer(owner));
        session.service.restore();
        return QTest::qWaitFor(
            [this, owner]() {
                return session.service.state() == AccountSessionState::Validated && session.service.profile().id == owner;
            },
            3000);
    }

    [[nodiscard]] QString marker() const { return session.settings.string(keys::PendingAccountCleanupOwnerId); }

    template <typename T>
    [[nodiscard]] static T complete(QFuture<T> future)
    {
        if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000)) {
            return T{};
        }
        return future.result();
    }

    AccountSessionFixture session;
    FakeLifecycleStorage storage;
    FakeOutboundSync sync;
    AccountDataLifecycleService lifecycle;
};

} // namespace

// Signing out: the last sync, the service's logout, the account's data
// here, and finishing an interrupted clean-up at the next launch.
class AccountSignOutTest : public QObject {
    Q_OBJECT

private slots:
    void successfulSignOutCleansUp();
    void blockedChangesDoNotStopSignOut();
    void failedFinalSyncKeepsEverything();
    void failedLogoutKeepsEverything();
    void expiredSessionDuringSyncKeepsData();
    void waitsForRunningWorkAndBlocksANewSignIn();
    void lateChangeStopsSignOut();
    void deferredCleanupKeepsItsMarker();
    void startupRecovery();
};

void AccountSignOutTest::successfulSignOutCleansUp()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::ok("{}"));
    fixture.storage.owners = {QStringLiteral("owner-a"), QStringLiteral("owner-b")};
    fixture.storage.pending.insert(QStringLiteral("owner-a"), 3);
    fixture.sync.onRequest = [&fixture]() { fixture.storage.pending.insert(QStringLiteral("owner-a"), 0); };
    QSignalSpy removed(&fixture.lifecycle, &AccountDataLifecycleService::accountDataRemoved);

    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::Succeeded);
    QVERIFY(result.signedOut);
    QVERIFY(result.safeError.isEmpty());
    QCOMPARE(fixture.sync.requests, 1);
    QCOMPARE(fixture.session.transport.requestCount(kLogoutPath), 1);
    QCOMPARE(fixture.session.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.session.storedSession(), QStringLiteral("<none>"));
    QVERIFY(!fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-b")));
    QVERIFY(fixture.marker().isEmpty());
    QVERIFY(fixture.session.gate.isAccepting());
    QCOMPARE(removed.count(), 1);
}

void AccountSignOutTest::blockedChangesDoNotStopSignOut()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::ok("{}"));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    fixture.storage.pending.insert(QStringLiteral("owner-a"), 1);
    fixture.sync.succeeds = false;
    // The service refused it for good: blocked rows do not count.
    fixture.sync.onRequest = [&fixture]() { fixture.storage.pending.insert(QStringLiteral("owner-a"), 0); };
    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::Succeeded);
    QVERIFY(!fixture.storage.owners.contains(QStringLiteral("owner-a")));
}

void AccountSignOutTest::failedFinalSyncKeepsEverything()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    fixture.storage.pending.insert(QStringLiteral("owner-a"), 2);
    fixture.sync.succeeds = false;
    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::FinalSyncFailed);
    QVERIFY(!result.signedOut);
    QVERIFY(!result.safeError.isEmpty());
    QCOMPARE(fixture.session.transport.requestCount(kLogoutPath), 0);
    QCOMPARE(fixture.session.service.profile().id, QStringLiteral("owner-a"));
    QCOMPARE(fixture.session.storedSession(), QStringLiteral("bearer-a"));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.marker().isEmpty());
    QVERIFY(fixture.session.gate.isAccepting());
}

void AccountSignOutTest::failedLogoutKeepsEverything()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::failed(lmp::core::FailureKind::Timeout));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::RemoteLogoutFailed);
    QVERIFY(!result.signedOut);
    QCOMPARE(fixture.session.service.profile().id, QStringLiteral("owner-a"));
    QCOMPARE(fixture.session.storedSession(), QStringLiteral("bearer-a"));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.marker().isEmpty());
    QVERIFY(fixture.session.gate.isAccepting());

    // A 401 means the service already ended it: that signs out.
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::failed(lmp::core::FailureKind::Unauthorized, 401));
    QCOMPARE(LifecycleFixture::complete(fixture.lifecycle.signOut()).outcome, AccountLogoutOutcome::Succeeded);
}

void AccountSignOutTest::expiredSessionDuringSyncKeepsData()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    fixture.storage.pending.insert(QStringLiteral("owner-a"), 1);
    const std::optional<lmp::core::account::AccountOperationContext> context =
        fixture.session.service.captureOperation();
    fixture.sync.succeeds = false;
    fixture.sync.onRequest = [&fixture, context]() { fixture.session.service.handleUnauthorized(*context); };
    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::FinalSyncFailed);
    QVERIFY(result.signedOut);
    QCOMPARE(fixture.session.transport.requestCount(kLogoutPath), 0);
    QCOMPARE(fixture.session.storedSession(), QStringLiteral("<none>"));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.marker().isEmpty());
}

void AccountSignOutTest::waitsForRunningWorkAndBlocksANewSignIn()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::ok("{}"));
    fixture.storage.owners = {QStringLiteral("owner-a"), QStringLiteral("owner-b")};
    std::optional<lmp::core::UserDataOperationGate::Lease> running = fixture.session.gate.tryEnter();
    QVERIFY(running.has_value());
    QFuture<AccountLogoutResult> operation = fixture.lifecycle.signOut();
    QTRY_VERIFY(!fixture.session.gate.isAccepting());
    QVERIFY(!fixture.session.gate.tryEnter().has_value());
    // A sign-in now is refused while the clean-up holds the gate.
    QFuture<Result<void>> signIn = fixture.session.service.beginSignIn();
    QTRY_VERIFY(signIn.isFinished());
    QVERIFY(!signIn.result().isOk());
    QTest::qWait(150);
    QVERIFY(!operation.isFinished());
    running.reset();
    const AccountLogoutResult result = LifecycleFixture::complete(operation);
    QCOMPARE(result.outcome, AccountLogoutOutcome::Succeeded);
    QVERIFY(!fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-b")));
}

void AccountSignOutTest::lateChangeStopsSignOut()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    std::optional<lmp::core::UserDataOperationGate::Lease> running = fixture.session.gate.tryEnter();
    QFuture<AccountLogoutResult> operation = fixture.lifecycle.signOut();
    QTRY_VERIFY(!fixture.session.gate.isAccepting());
    // The running work queued a change after the final sync.
    fixture.storage.pending.insert(QStringLiteral("owner-a"), 1);
    running.reset();
    const AccountLogoutResult result = LifecycleFixture::complete(operation);
    QCOMPARE(result.outcome, AccountLogoutOutcome::FinalSyncFailed);
    QVERIFY(!result.signedOut);
    QCOMPARE(fixture.session.transport.requestCount(kLogoutPath), 0);
    QCOMPARE(fixture.session.service.profile().id, QStringLiteral("owner-a"));
    QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-a")));
    QVERIFY(fixture.session.gate.isAccepting());
}

void AccountSignOutTest::deferredCleanupKeepsItsMarker()
{
    LifecycleFixture fixture;
    QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    fixture.session.transport.script(kLogoutPath, FakeHttpTransport::ok("{}"));
    fixture.storage.owners = {QStringLiteral("owner-a")};
    fixture.storage.failRemove = true;
    const AccountLogoutResult result = LifecycleFixture::complete(fixture.lifecycle.signOut());
    QCOMPARE(result.outcome, AccountLogoutOutcome::CleanupDeferred);
    QVERIFY(result.signedOut);
    QCOMPARE(fixture.marker(), QStringLiteral("owner-a"));
    QVERIFY(fixture.session.gate.isAccepting());
}

void AccountSignOutTest::startupRecovery()
{
    {
        // Signed out, data left behind: the next launch removes it.
        LifecycleFixture fixture;
        fixture.storage.owners = {QStringLiteral("owner-a")};
        QVERIFY(fixture.session.settings.trySetString(keys::PendingAccountCleanupOwnerId, QStringLiteral("owner-a")));
        const AccountCleanupResult result = LifecycleFixture::complete(fixture.lifecycle.resolvePendingCleanup());
        QCOMPARE(result.outcome, AccountCleanupOutcome::Cleaned);
        QVERIFY(fixture.storage.owners.isEmpty());
        QVERIFY(fixture.marker().isEmpty());
        QVERIFY(fixture.session.gate.isAccepting());
    }
    {
        // The same account is signed in: its data stays.
        LifecycleFixture fixture;
        QVERIFY(fixture.signIn(QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
        fixture.storage.owners = {QStringLiteral("owner-a")};
        QVERIFY(fixture.session.settings.trySetString(keys::PendingAccountCleanupOwnerId, QStringLiteral("owner-a")));
        const AccountCleanupResult result = LifecycleFixture::complete(fixture.lifecycle.resolvePendingCleanup());
        QCOMPARE(result.outcome, AccountCleanupOutcome::PreservedAuthenticatedAccount);
        QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-a")));
        QCOMPARE(fixture.session.storedSession(), QStringLiteral("bearer-a"));
        QVERIFY(fixture.marker().isEmpty());
    }
    {
        // Another account is signed in: only the signed-out one goes.
        LifecycleFixture fixture;
        QVERIFY(fixture.signIn(QStringLiteral("bearer-b"), QStringLiteral("owner-b")));
        fixture.storage.owners = {QStringLiteral("owner-a"), QStringLiteral("owner-b")};
        QVERIFY(fixture.session.settings.trySetString(keys::PendingAccountCleanupOwnerId, QStringLiteral("owner-a")));
        const AccountCleanupResult result = LifecycleFixture::complete(fixture.lifecycle.resolvePendingCleanup());
        QCOMPARE(result.outcome, AccountCleanupOutcome::Cleaned);
        QVERIFY(!fixture.storage.owners.contains(QStringLiteral("owner-a")));
        QVERIFY(fixture.storage.owners.contains(QStringLiteral("owner-b")));
        QCOMPARE(fixture.session.service.profile().id, QStringLiteral("owner-b"));
    }
    {
        // A marker that cannot be an owner id is removed.
        LifecycleFixture fixture;
        QVERIFY(fixture.session.settings.trySetString(keys::PendingAccountCleanupOwnerId, QStringLiteral("a\tb")));
        QCOMPARE(LifecycleFixture::complete(fixture.lifecycle.resolvePendingCleanup()).outcome,
                 AccountCleanupOutcome::InvalidMarkerRemoved);
        QVERIFY(fixture.marker().isEmpty());
    }
    {
        LifecycleFixture fixture;
        QCOMPARE(LifecycleFixture::complete(fixture.lifecycle.resolvePendingCleanup()).outcome,
                 AccountCleanupOutcome::NothingPending);
    }
}

QTEST_GUILESS_MAIN(AccountSignOutTest)
#include "tst_account_sign_out.moc"
