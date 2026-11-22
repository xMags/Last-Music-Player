#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <optional>

#include "AccountSessionFixture.h"
#include "core/settings/SettingKeys.h"

using lmp::core::FailureKind;
using lmp::core::account::AccountOperationContext;
using lmp::core::account::AccountSessionService;
using lmp::core::account::AccountSessionState;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
using lmp::test::kProfilePath;
namespace keys = lmp::core::settings::keys;

namespace {

// Restores a stored session the service confirms.
[[nodiscard]] bool establish(AccountSessionFixture& fixture, const QString& bearer, const QString& owner)
{
    fixture.storeSession(bearer);
    fixture.transport.script(kProfilePath, AccountSessionFixture::profileAnswer(owner));
    fixture.service.restore();
    return QTest::qWaitFor([&fixture]() { return fixture.service.state() == AccountSessionState::Validated; }, 3000)
        && fixture.service.profile().id == owner;
}

} // namespace

class AccountSessionRestoreTest : public QObject {
    Q_OBJECT

private slots:
    void noCredentialSignsOut();
    void unreadableCredentialCountsAsNone();
    void unconfiguredBuildUsesTheHints();
    void unconfiguredBuildWithoutHintsSignsOut();
    void confirmedSessionIsValidated();
    void rejectedSessionIsRemoved_data();
    void rejectedSessionIsRemoved();
    void unreachableServiceUsesTheHints_data();
    void unreachableServiceUsesTheHints();
    void unreachableServiceWithoutHintsSignsOut();
    void replacedCredentialIsNotValidated();
    void restoreCannotResurrectARemovedSession();
    void closedGateRefusesRestore();
    void unauthorizedOnlyClearsItsOwnSession();
    void failedRemovalKeepsTheSession();
};

void AccountSessionRestoreTest::noCredentialSignsOut()
{
    AccountSessionFixture fixture;
    fixture.setHints(QStringLiteral("owner-a"));
    QSignalSpy changes(&fixture.service, &AccountSessionService::stateChanged);
    fixture.service.restore();
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(fixture.transport.requests.size(), 0);
}

void AccountSessionRestoreTest::unreadableCredentialCountsAsNone()
{
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.setHints(QStringLiteral("owner-a"));
    fixture.credentials.failRead = true;
    fixture.service.restore();
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.transport.requests.size(), 0);
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
}

void AccountSessionRestoreTest::unconfiguredBuildUsesTheHints()
{
    AccountSessionFixture fixture{QString()};
    QVERIFY(!fixture.service.isAvailable());
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.setHints(QStringLiteral("owner-a"));
    fixture.service.restore();
    QCOMPARE(fixture.service.state(), AccountSessionState::Offline);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-a"));
    QCOMPARE(fixture.service.profile().displayName, QStringLiteral("Hinted owner-a"));
    QCOMPARE(fixture.service.profile().planLabel, QStringLiteral("Hinted plan"));
    QVERIFY(fixture.service.lastFailure().has_value());
    QCOMPARE(fixture.service.lastFailure()->kind, FailureKind::NotAvailable);
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
    QVERIFY(fixture.service.captureOperation().has_value());
}

void AccountSessionRestoreTest::unconfiguredBuildWithoutHintsSignsOut()
{
    AccountSessionFixture fixture{QString()};
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.service.restore();
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
    QVERIFY(!fixture.service.captureOperation().has_value());
}

void AccountSessionRestoreTest::confirmedSessionIsValidated()
{
    AccountSessionFixture fixture;
    QSignalSpy owners(&fixture.service, &AccountSessionService::ownerChanged);
    QVERIFY(establish(fixture, QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    QCOMPARE(fixture.transport.requests.front().header("Authorization"), QByteArray("Bearer bearer-a"));
    QCOMPARE(fixture.service.profile().displayName, QStringLiteral("Name owner-a"));
    QVERIFY(!fixture.service.lastFailure().has_value());
    QCOMPARE(fixture.settings.string(keys::AccountOwnerHint), QStringLiteral("owner-a"));
    QCOMPARE(fixture.settings.string(keys::AccountDisplayNameHint), QStringLiteral("Name owner-a"));
    QCOMPARE(fixture.settings.string(keys::AccountUsernameHint), QStringLiteral("user"));
    QCOMPARE(fixture.settings.string(keys::AccountPlanHint), QStringLiteral("Test"));
    QCOMPARE(owners.size(), 1);
    QCOMPARE(owners.front().front().toString(), QStringLiteral("owner-a"));
    const std::optional<AccountOperationContext> context = fixture.service.captureOperation();
    QVERIFY(context.has_value());
    QCOMPARE(context->bearerSession, QStringLiteral("bearer-a"));
    QVERIFY(fixture.service.isCurrent(*context));
}

void AccountSessionRestoreTest::rejectedSessionIsRemoved_data()
{
    QTest::addColumn<int>("status");
    QTest::newRow("401") << 401;
    QTest::newRow("403") << 403;
}

void AccountSessionRestoreTest::rejectedSessionIsRemoved()
{
    QFETCH(int, status);
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.setHints(QStringLiteral("owner-a"));
    fixture.service.restore();
    QVERIFY(fixture.transport.answer(kProfilePath, FakeHttpTransport::failed(
        status == 401 ? FailureKind::Unauthorized : FailureKind::Forbidden, status)));
    QTRY_COMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QVERIFY(fixture.service.profile().id.isEmpty());
}

void AccountSessionRestoreTest::unreachableServiceUsesTheHints_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("status");
    QTest::newRow("no connection") << static_cast<int>(FailureKind::Network) << 0;
    QTest::newRow("timeout") << static_cast<int>(FailureKind::Timeout) << 0;
    QTest::newRow("server error") << static_cast<int>(FailureKind::Server) << 503;
    QTest::newRow("rate limited") << static_cast<int>(FailureKind::RateLimited) << 429;
    QTest::newRow("not found") << static_cast<int>(FailureKind::NotFound) << 404;
}

void AccountSessionRestoreTest::unreachableServiceUsesTheHints()
{
    QFETCH(int, kind);
    QFETCH(int, status);
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.setHints(QStringLiteral("owner-a"));
    fixture.transport.script(kProfilePath, FakeHttpTransport::failed(static_cast<FailureKind>(kind), status));
    fixture.service.restore();
    QTRY_COMPARE(fixture.service.state(), AccountSessionState::Offline);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-a"));
    QCOMPARE(fixture.service.lastFailure()->kind, static_cast<FailureKind>(kind));
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
}

void AccountSessionRestoreTest::unreachableServiceWithoutHintsSignsOut()
{
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    // A profile without an owner is a failed check, not a rejection.
    fixture.transport.script(kProfilePath, FakeHttpTransport::ok("{\"displayName\":\"x\"}"));
    fixture.service.restore();
    QTRY_COMPARE(fixture.transport.requests.size(), 1);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
}

void AccountSessionRestoreTest::replacedCredentialIsNotValidated()
{
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    fixture.service.restore();
    fixture.storeSession(QStringLiteral("bearer-b"));
    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QTRY_COMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-b"));
}

void AccountSessionRestoreTest::restoreCannotResurrectARemovedSession()
{
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("restore-a"));
    QSignalSpy owners(&fixture.service, &AccountSessionService::ownerChanged);
    fixture.service.restore();
    QCOMPARE(fixture.transport.waitingCount(kProfilePath), 1);

    QVERIFY(fixture.service.removeLocalSession());
    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QTest::qWait(20);

    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QVERIFY(fixture.service.profile().id.isEmpty());
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(owners.size(), 0);
}

void AccountSessionRestoreTest::closedGateRefusesRestore()
{
    AccountSessionFixture fixture;
    fixture.storeSession(QStringLiteral("bearer-a"));
    QVERIFY(fixture.gate.closeAdmissions());
    QSignalSpy changes(&fixture.service, &AccountSessionService::stateChanged);
    fixture.service.restore();
    QCOMPARE(changes.size(), 0);
    QCOMPARE(fixture.credentials.readCount, 0);
}

void AccountSessionRestoreTest::unauthorizedOnlyClearsItsOwnSession()
{
    AccountSessionFixture fixture;
    QVERIFY(establish(fixture, QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    const AccountOperationContext oldContext = *fixture.service.captureOperation();

    QVERIFY(establish(fixture, QStringLiteral("bearer-b"), QStringLiteral("owner-b")));
    QVERIFY(!fixture.service.isCurrent(oldContext));
    fixture.service.handleUnauthorized(oldContext);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-b"));
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-b"));

    const AccountOperationContext currentContext = *fixture.service.captureOperation();
    fixture.service.handleUnauthorized(currentContext);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QVERIFY(!fixture.service.isCurrent(currentContext));
}

void AccountSessionRestoreTest::failedRemovalKeepsTheSession()
{
    AccountSessionFixture fixture;
    QVERIFY(establish(fixture, QStringLiteral("bearer-a"), QStringLiteral("owner-a")));
    const AccountOperationContext context = *fixture.service.captureOperation();

    fixture.credentials.failRemove = true;
    QVERIFY(!fixture.service.removeLocalSession());
    QCOMPARE(fixture.service.state(), AccountSessionState::Validated);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-a"));
    QVERIFY(fixture.service.isCurrent(context));
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));

    // A 401 whose credential cannot be removed leaves the session as well.
    fixture.service.handleUnauthorized(context);
    QCOMPARE(fixture.service.state(), AccountSessionState::Validated);
    QVERIFY(fixture.service.isCurrent(context));
}

QTEST_GUILESS_MAIN(AccountSessionRestoreTest)
#include "tst_account_session_restore.moc"
