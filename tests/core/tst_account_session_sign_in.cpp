#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include "AccountSessionFixture.h"
#include "core/account/Pkce.h"
#include "core/app/AppIdentity.h"
#include "core/net/UserMessages.h"

using lmp::core::AppIdentity;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::account::AccountSessionService;
using lmp::core::account::AccountSessionState;
using lmp::core::account::createPkceChallenge;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
using lmp::test::kProfilePath;
using lmp::test::kTokenPath;

namespace {

using SignIn = QFuture<Result<void>>;

[[nodiscard]] bool finishes(const SignIn& future)
{
    return QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000);
}

[[nodiscard]] FailureKind failureOf(const SignIn& future)
{
    return future.result().isOk() ? FailureKind::Unknown : future.result().failure().kind;
}

// Starts a sign-in and brings it to the point where its profile request
// waits for an answer.
[[nodiscard]] bool signInUntilProfile(AccountSessionFixture& fixture, SignIn& future, const QString& bearer)
{
    const int profilesBefore = fixture.transport.waitingCount(kProfilePath);
    fixture.transport.script(kTokenPath, AccountSessionFixture::tokenAnswer(bearer));
    future = fixture.service.beginSignIn();
    return fixture.returnFromBrowser("code-" + bearer.toUtf8())
        && QTest::qWaitFor([&fixture, profilesBefore]() {
               return fixture.transport.waitingCount(kProfilePath) == profilesBefore + 1;
           }, 3000);
}

} // namespace

class AccountSessionSignInTest : public QObject {
    Q_OBJECT

private slots:
    void signInStoresAndValidatesTheSession();
    void newerSignInSupersedesAnOlderOne();
    void supersededWaitClosesItsListener();
    void failedReplacementLeavesNoSession();
    void cancelledSignInEndsSignedOut();
    void failedReadBackRemovesTheSession();
    void failedWriteStoresNothing();
    void refusedBrowserEndsTheAttempt();
    void exchangeFailureIsReported();
    void unconfiguredBuildCannotSignIn();
    void closedGateRefusesSignIn();
};

void AccountSessionSignInTest::signInStoresAndValidatesTheSession()
{
    AccountSessionFixture fixture;
    QSignalSpy owners(&fixture.service, &AccountSessionService::ownerChanged);
    fixture.transport.script(kTokenPath, AccountSessionFixture::tokenAnswer(QStringLiteral("bearer-a")));
    fixture.transport.script(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a")));

    const SignIn future = fixture.service.beginSignIn();
    QCOMPARE(fixture.service.state(), AccountSessionState::SigningIn);
    QCOMPARE(fixture.links.opened.size(), 1);
    const QUrl opened = fixture.links.opened.front();
    QCOMPARE(opened.scheme(), QStringLiteral("https"));
    QCOMPARE(opened.path(), QStringLiteral("/v1/sso/authorize"));
    QCOMPARE(fixture.authorizeValue(QStringLiteral("client_id")), AppIdentity::accountClientId());
    QCOMPARE(fixture.authorizeValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
    const QString redirect = fixture.authorizeValue(QStringLiteral("redirect_uri"));
    QVERIFY(redirect.startsWith(QStringLiteral("http://")));
    QVERIFY(redirect.endsWith(AppIdentity::accountCallbackPath()));

    QVERIFY(fixture.returnFromBrowser("code-a"));
    QVERIFY(finishes(future));
    QVERIFY(future.result().isOk());
    QCOMPARE(fixture.service.state(), AccountSessionState::Validated);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-a"));
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-a"));
    QCOMPARE(owners.size(), 1);

    const QJsonObject exchange = QJsonDocument::fromJson(fixture.transport.requests.front().body).object();
    QCOMPARE(exchange.value(QStringLiteral("code")).toString(), QStringLiteral("code-a"));
    QCOMPARE(exchange.value(QStringLiteral("redirect_uri")).toString(), redirect);
    const Result<QString> challenge = createPkceChallenge(exchange.value(QStringLiteral("code_verifier")).toString());
    QVERIFY(challenge);
    QCOMPARE(challenge.value(), fixture.authorizeValue(QStringLiteral("code_challenge")));
    QCOMPARE(fixture.transport.requests.at(1).header("Authorization"), QByteArray("Bearer bearer-a"));
}

void AccountSessionSignInTest::newerSignInSupersedesAnOlderOne()
{
    AccountSessionFixture fixture;
    QSignalSpy owners(&fixture.service, &AccountSessionService::ownerChanged);
    SignIn first;
    QVERIFY(signInUntilProfile(fixture, first, QStringLiteral("bearer-a")));

    fixture.transport.script(kTokenPath, AccountSessionFixture::tokenAnswer(QStringLiteral("bearer-b")));
    fixture.transport.script(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-b")));
    const SignIn second = fixture.service.beginSignIn();
    QVERIFY(finishes(first));
    QCOMPARE(failureOf(first), FailureKind::Cancelled);
    QVERIFY(fixture.returnFromBrowser("code-b"));
    QVERIFY(finishes(second));
    QVERIFY(second.result().isOk());

    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QTest::qWait(20);
    QCOMPARE(fixture.service.state(), AccountSessionState::Validated);
    QCOMPARE(fixture.service.profile().id, QStringLiteral("owner-b"));
    QCOMPARE(fixture.storedSession(), QStringLiteral("bearer-b"));
    QCOMPARE(owners.size(), 1);
    QCOMPARE(owners.front().front().toString(), QStringLiteral("owner-b"));
    QVERIFY(!fixture.service.lastFailure().has_value());
}

void AccountSessionSignInTest::supersededWaitClosesItsListener()
{
    AccountSessionFixture fixture;
    const SignIn first = fixture.service.beginSignIn();
    const QUrl firstAddress = fixture.links.opened.last();
    const SignIn second = fixture.service.beginSignIn();
    QVERIFY(finishes(first));
    QCOMPARE(failureOf(first), FailureKind::Cancelled);
    QCOMPARE(fixture.service.state(), AccountSessionState::SigningIn);
    QVERIFY(!second.isFinished());
    QVERIFY(fixture.links.opened.last() != firstAddress);
    QCOMPARE(fixture.transport.requests.size(), 0);

    fixture.service.cancelSignIn();
    QVERIFY(finishes(second));
}

void AccountSessionSignInTest::failedReplacementLeavesNoSession()
{
    AccountSessionFixture fixture;
    QSignalSpy owners(&fixture.service, &AccountSessionService::ownerChanged);
    SignIn first;
    QVERIFY(signInUntilProfile(fixture, first, QStringLiteral("bearer-a")));

    fixture.transport.script(kTokenPath, FakeHttpTransport::failed(FailureKind::Unknown, 400));
    const SignIn second = fixture.service.beginSignIn();
    QVERIFY(fixture.returnFromBrowser("code-b"));
    QVERIFY(finishes(second));
    QVERIFY(!second.result().isOk());

    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QTest::qWait(20);
    QCOMPARE(failureOf(first), FailureKind::Cancelled);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QVERIFY(fixture.service.profile().id.isEmpty());
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(owners.size(), 0);
    QVERIFY(lmp::core::net::userMessageForSignInFailure(*fixture.service.lastFailure())
            == lmp::core::net::UserMessage::SignInFailed);
}

void AccountSessionSignInTest::cancelledSignInEndsSignedOut()
{
    AccountSessionFixture fixture;
    const SignIn future = fixture.service.beginSignIn();
    fixture.service.cancelSignIn();
    QVERIFY(finishes(future));
    QCOMPARE(failureOf(future), FailureKind::Cancelled);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.service.lastFailure()->kind, FailureKind::Cancelled);
    QVERIFY(!lmp::core::net::userMessageForSignInFailure(*fixture.service.lastFailure()).has_value());
    QVERIFY(!fixture.returnFromBrowser("code-late"));
    QCOMPARE(fixture.transport.requests.size(), 0);
}

void AccountSessionSignInTest::failedReadBackRemovesTheSession()
{
    AccountSessionFixture fixture;
    SignIn future;
    QVERIFY(signInUntilProfile(fixture, future, QStringLiteral("bearer-a")));
    fixture.credentials.corruptNextRead = true;
    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QVERIFY(finishes(future));
    QCOMPARE(failureOf(future), FailureKind::Storage);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(fixture.credentials.writeCount, 1);
}

void AccountSessionSignInTest::failedWriteStoresNothing()
{
    AccountSessionFixture fixture;
    fixture.credentials.failWrite = true;
    SignIn future;
    QVERIFY(signInUntilProfile(fixture, future, QStringLiteral("bearer-a")));
    QVERIFY(fixture.transport.answer(kProfilePath, AccountSessionFixture::profileAnswer(QStringLiteral("owner-a"))));
    QVERIFY(finishes(future));
    QCOMPARE(failureOf(future), FailureKind::Storage);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(fixture.credentials.conditionalRemoveCount, 0);
}

void AccountSessionSignInTest::refusedBrowserEndsTheAttempt()
{
    AccountSessionFixture fixture;
    fixture.links.accept = false;
    const SignIn future = fixture.service.beginSignIn();
    QVERIFY(finishes(future));
    QCOMPARE(failureOf(future), FailureKind::Unknown);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.links.opened.size(), 1);
    QVERIFY(!fixture.returnFromBrowser("code-a"));
}

void AccountSessionSignInTest::exchangeFailureIsReported()
{
    AccountSessionFixture fixture;
    fixture.transport.script(kTokenPath, FakeHttpTransport::failed(FailureKind::Network));
    const SignIn future = fixture.service.beginSignIn();
    QVERIFY(fixture.returnFromBrowser("code-a"));
    QVERIFY(finishes(future));
    QCOMPARE(failureOf(future), FailureKind::Network);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QVERIFY(lmp::core::net::userMessageForSignInFailure(*fixture.service.lastFailure())
            == lmp::core::net::UserMessage::Connection);
    QCOMPARE(fixture.transport.requestCount(kProfilePath), 0);
}

void AccountSessionSignInTest::unconfiguredBuildCannotSignIn()
{
    AccountSessionFixture fixture{QString()};
    QSignalSpy changes(&fixture.service, &AccountSessionService::stateChanged);
    const SignIn future = fixture.service.beginSignIn();
    QVERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::NotAvailable);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.service.lastFailure()->kind, FailureKind::NotAvailable);
    QCOMPARE(fixture.links.opened.size(), 0);
    QCOMPARE(changes.size(), 1);
}

void AccountSessionSignInTest::closedGateRefusesSignIn()
{
    AccountSessionFixture fixture;
    QVERIFY(fixture.gate.closeAdmissions());
    const SignIn future = fixture.service.beginSignIn();
    QVERIFY(future.isFinished());
    QCOMPARE(failureOf(future), FailureKind::NotAvailable);
    QCOMPARE(fixture.service.state(), AccountSessionState::SignedOut);
    QCOMPARE(fixture.links.opened.size(), 0);
}

QTEST_GUILESS_MAIN(AccountSessionSignInTest)
#include "tst_account_session_sign_in.moc"
