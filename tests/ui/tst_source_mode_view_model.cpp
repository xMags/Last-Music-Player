#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QVariantMap>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/async/Result.h"
#include "core/mode/SourceMode.h"
#include "core/net/UserMessages.h"
#include "ui/settings/SourceModeViewModel.h"

using lmp::core::FailureKind;
using lmp::core::mode::SourceMode;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
using lmp::test::kProfilePath;
using lmp::test::kProviderBase;
using lmp::test::kProviderStatusPath;
using lmp::test::kTokenPath;
using lmp::ui::SourceModeViewModel;

// The music source cards, API key form, account block and sign-in follow the
// current app's rules, over a fake network and credential store.
class SourceModeViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void cardsShowTheStartState();
    void apiKeyWithoutProviderShowsTheForm();
    void accountWithoutSessionAsksForSignIn();
    void connectReportsEachOutcome();
    void connectSucceedsAndEntersApiKeyMode();
    void disconnectFallsBackToOffline();
    void signInSwitchesToAccount();
    void signOutReturnsToOffline();
    void cancelledSignInShowsNoError();
    void failedSignInShowsWhy();

private:
    [[nodiscard]] QVariantMap card(int index) const { return m_model->cards().at(index).toMap(); }
    [[nodiscard]] bool signIn(const QString& owner);

    std::unique_ptr<lmp::test::ModeServiceFixture> m_fixture;
    std::unique_ptr<lmp::core::account::LocalAccountSignOut> m_signOut;
    std::unique_ptr<SourceModeViewModel> m_model;
};

void SourceModeViewModelTest::init()
{
    m_fixture = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_fixture->modes.start();
    m_signOut = std::make_unique<lmp::core::account::LocalAccountSignOut>(m_fixture->session.service);
    m_model = std::make_unique<SourceModeViewModel>(SourceModeViewModel::Parts{
        .mode = m_fixture->modes,
        .session = m_fixture->session.service,
        .accountClient = m_fixture->session.client,
        .signOut = *m_signOut,
        .links = m_fixture->session.links,
        .library = nullptr,
    });
}

void SourceModeViewModelTest::cleanup()
{
    m_model.reset();
    m_signOut.reset();
    m_fixture.reset();
}

bool SourceModeViewModelTest::signIn(const QString& owner)
{
    QSignalSpy finished(m_model.get(), &SourceModeViewModel::signInFinished);
    m_fixture->session.transport.script(kTokenPath, AccountSessionFixture::tokenAnswer(QStringLiteral("bearer-") + owner));
    m_fixture->session.transport.script(kProfilePath, AccountSessionFixture::profileAnswer(owner));
    m_model->beginSignIn();
    return m_fixture->session.returnFromBrowser("code-" + owner.toUtf8())
        && QTest::qWaitFor([&finished]() { return finished.count() == 1; }, 3000);
}

void SourceModeViewModelTest::cardsShowTheStartState()
{
    QCOMPARE(m_model->mode(), QStringLiteral("LocalOnly"));
    QCOMPARE(m_model->cards().size(), 3);

    const QVariantMap offline = card(0);
    QCOMPARE(offline.value(QStringLiteral("mode")).toString(), QStringLiteral("LocalOnly"));
    QVERIFY(offline.value(QStringLiteral("selected")).toBool());
    QCOMPARE(offline.value(QStringLiteral("badge")).toString(), QStringLiteral("ACTIVE"));

    const QVariantMap account = card(1);
    QCOMPARE(account.value(QStringLiteral("mode")).toString(), QStringLiteral("Account"));
    QVERIFY(account.value(QStringLiteral("enabled")).toBool());
    QCOMPARE(account.value(QStringLiteral("badge")).toString(), QStringLiteral("SET UP"));
    QCOMPARE(account.value(QStringLiteral("status")).toString(), QStringLiteral("Not signed in"));

    const QVariantMap apiKey = card(2);
    QCOMPARE(apiKey.value(QStringLiteral("mode")).toString(), QStringLiteral("ApiKey"));
    QCOMPARE(apiKey.value(QStringLiteral("badge")).toString(), QStringLiteral("SET UP"));
    QCOMPARE(apiKey.value(QStringLiteral("status")).toString(), QStringLiteral("No provider configured"));
    QVERIFY(!apiKey.value(QStringLiteral("statusReady")).toBool());

    QVERIFY(!m_model->apiKeyShown());
    QVERIFY(!m_model->accountShown());
}

void SourceModeViewModelTest::apiKeyWithoutProviderShowsTheForm()
{
    // Its address and key are typed into the form this brings up.
    m_model->choose(QStringLiteral("ApiKey"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::ApiKey);
    QVERIFY(m_model->apiKeyShown());
    QVERIFY(!m_model->providerConnected());
    QCOMPARE(m_model->providerBaseUrl(), QStringLiteral("http://127.0.0.1:4527"));

    m_model->choose(QStringLiteral("LocalOnly"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
    m_model->choose(QStringLiteral("Nonsense"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
}

void SourceModeViewModelTest::accountWithoutSessionAsksForSignIn()
{
    QSignalSpy requested(m_model.get(), &SourceModeViewModel::signInRequested);
    m_model->choose(QStringLiteral("Account"));
    QCOMPARE(requested.count(), 1);
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
}

void SourceModeViewModelTest::connectReportsEachOutcome()
{
    QSignalSpy status(m_model.get(), &SourceModeViewModel::apiKeyStatusChanged);
    m_model->connectProvider(kProviderBase, QString());
    QVERIFY(QTest::qWaitFor([this]() { return !m_model->connecting(); }, 3000));
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Enter a new API key"));

    m_model->connectProvider(QString(), QStringLiteral("key"));
    QVERIFY(QTest::qWaitFor([this]() { return !m_model->connecting(); }, 3000));
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Missing provider URL"));

    m_fixture->session.transport.script(kProviderStatusPath, FakeHttpTransport::failed(FailureKind::Unauthorized, 401));
    m_model->connectProvider(kProviderBase, QStringLiteral("key"));
    QVERIFY(QTest::qWaitFor([this]() { return !m_model->connecting(); }, 3000));
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Unauthorized"));

    m_fixture->session.transport.script(kProviderStatusPath, FakeHttpTransport::failed(FailureKind::Network));
    m_model->connectProvider(kProviderBase, QStringLiteral("key"));
    QVERIFY(QTest::qWaitFor([this]() { return !m_model->connecting(); }, 3000));
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Provider unavailable"));

    // Nothing was stored by any of them.
    QCOMPARE(m_fixture->storedKey(), QStringLiteral("<none>"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
    QVERIFY(status.count() >= 8);
}

void SourceModeViewModelTest::connectSucceedsAndEntersApiKeyMode()
{
    QSignalSpy connected(m_model.get(), &SourceModeViewModel::connected);
    m_fixture->session.transport.script(kProviderStatusPath, FakeHttpTransport::ok("{}"));
    QSignalSpy status(m_model.get(), &SourceModeViewModel::apiKeyStatusChanged);
    m_model->connectProvider(kProviderBase, QStringLiteral("key-a"));
    // "Connecting..." first, whenever the answer comes.
    QVERIFY(status.count() >= 1);
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 3000);
    QVERIFY(!m_model->connecting());
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Configured"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::ApiKey);
    QVERIFY(m_model->providerConnected());
    QCOMPARE(m_model->providerBaseUrl(), kProviderBase);
    QCOMPARE(m_fixture->storedKey(), QStringLiteral("key-a"));

    const QVariantMap apiKey = card(2);
    QCOMPARE(apiKey.value(QStringLiteral("badge")).toString(), QStringLiteral("ACTIVE"));
    QCOMPARE(apiKey.value(QStringLiteral("status")).toString(), QStringLiteral("Provider configured"));
    QVERIFY(apiKey.value(QStringLiteral("statusReady")).toBool());
    QCOMPARE(card(0).value(QStringLiteral("badge")).toString(), QStringLiteral("READY"));
}

void SourceModeViewModelTest::disconnectFallsBackToOffline()
{
    m_fixture->session.transport.script(kProviderStatusPath, FakeHttpTransport::ok("{}"));
    QSignalSpy connected(m_model.get(), &SourceModeViewModel::connected);
    m_model->connectProvider(kProviderBase, QStringLiteral("key-a"));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 3000);

    m_model->disconnectProvider();
    QCOMPARE(m_model->apiKeyStatus(), QStringLiteral("Not configured"));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
    QCOMPARE(m_fixture->storedKey(), QStringLiteral("<none>"));
    QVERIFY(!m_model->providerConnected());
}

void SourceModeViewModelTest::signInSwitchesToAccount()
{
    QVERIFY(signIn(QStringLiteral("owner-a")));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::Account);
    QVERIFY(m_model->accountShown());
    QVERIFY(m_model->signedIn());
    QCOMPARE(m_model->accountName(), QStringLiteral("Name owner-a"));
    QCOMPARE(m_model->accountUsername(), QStringLiteral("@user"));
    QCOMPARE(m_model->accountPlan(), QStringLiteral("Test"));
    QCOMPARE(m_model->accountStatus(), QStringLiteral("Connected"));
    QVERIFY(!m_model->accountStatusError());
    QCOMPARE(m_model->signInError(), QString());

    const QVariantMap account = card(1);
    QCOMPARE(account.value(QStringLiteral("badge")).toString(), QStringLiteral("ACTIVE"));
    QCOMPARE(account.value(QStringLiteral("status")).toString(), QStringLiteral("Signed in as @user"));
    // The browser was asked to open the service's sign-in page, nothing else.
    QCOMPARE(m_fixture->session.links.opened.size(), 1);
}

void SourceModeViewModelTest::signOutReturnsToOffline()
{
    QVERIFY(signIn(QStringLiteral("owner-a")));
    m_model->signOut();
    QVERIFY(QTest::qWaitFor([this]() { return m_fixture->modes.mode() == SourceMode::LocalOnly; }, 3000));
    QVERIFY(!m_model->signedIn());
    QVERIFY(!m_model->accountShown());
    QCOMPARE(m_fixture->session.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(card(1).value(QStringLiteral("badge")).toString(), QStringLiteral("SET UP"));
}

void SourceModeViewModelTest::cancelledSignInShowsNoError()
{
    m_model->beginSignIn();
    QVERIFY(QTest::qWaitFor([this]() { return m_model->signingIn(); }, 3000));
    m_model->cancelSignIn();
    QVERIFY(QTest::qWaitFor([this]() { return !m_model->signingIn(); }, 3000));
    QCOMPARE(m_model->signInError(), QString());
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
}

void SourceModeViewModelTest::failedSignInShowsWhy()
{
    m_fixture->session.transport.script(kTokenPath, FakeHttpTransport::failed(FailureKind::Server, 400));
    m_model->beginSignIn();
    QVERIFY(m_fixture->session.returnFromBrowser("code-x"));
    QTRY_VERIFY_WITH_TIMEOUT(!m_model->signInError().isEmpty(), 3000);
    QCOMPARE(m_model->signInError(), lmp::core::net::userMessageText(lmp::core::net::UserMessage::SignInFailed));
    QCOMPARE(m_fixture->modes.mode(), SourceMode::LocalOnly);
    QVERIFY(m_model->accountStatusError());

    m_model->clearSignInError();
    QCOMPARE(m_model->signInError(), QString());
}

QTEST_MAIN(SourceModeViewModelTest)
#include "tst_source_mode_view_model.moc"
