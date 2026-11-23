#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/mode/SourceMode.h"
#include "core/settings/SettingKeys.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/shell/ProfileViewModel.h"

using lmp::core::mode::SourceMode;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
using lmp::test::kProfilePath;
using lmp::ui::ProfileViewModel;
using lmp::ui::Route;
namespace keys = lmp::core::settings::keys;

// The sidebar profile in all three modes, its picture check and its menu.
class ProfileViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void offlineShowsTheTypedName();
    void apiKeyModeShowsTheTypedName();
    void accountModeShowsTheAccount();
    void unsafePicturesAreNotLoaded();
    void menuOffersSignOutOnlyWhenSignedIn();
    void initials_data();
    void initials();

private:
    // Restores a session the service confirms with the given profile JSON,
    // then enters Account mode.
    [[nodiscard]] bool enterAccount(const QByteArray& userJson);

    std::unique_ptr<lmp::test::ModeServiceFixture> m_fixture;
    std::unique_ptr<lmp::ui::Navigator> m_navigator;
    std::unique_ptr<lmp::core::account::LocalAccountSignOut> m_signOut;
    std::unique_ptr<ProfileViewModel> m_model;
};

void ProfileViewModelTest::init()
{
    m_fixture = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_fixture->modes.start();
    m_navigator = std::make_unique<lmp::ui::Navigator>();
    m_signOut = std::make_unique<lmp::core::account::LocalAccountSignOut>(m_fixture->session.service);
    m_model = std::make_unique<ProfileViewModel>(ProfileViewModel::Parts{
        .settings = m_fixture->session.settings,
        .mode = m_fixture->modes,
        .session = m_fixture->session.service,
        .signOut = *m_signOut,
        .navigator = *m_navigator,
    });
}

void ProfileViewModelTest::cleanup()
{
    m_model.reset();
    m_signOut.reset();
    m_navigator.reset();
    m_fixture.reset();
}

bool ProfileViewModelTest::enterAccount(const QByteArray& userJson)
{
    auto& session = m_fixture->session;
    session.storeSession(QStringLiteral("bearer-a"));
    session.transport.script(kProfilePath, FakeHttpTransport::ok("{\"user\":" + userJson + "}"));
    session.service.restore();
    if (!m_fixture->waitForSession(lmp::core::account::AccountSessionState::Validated)) {
        return false;
    }
    return m_fixture->modes.setMode(SourceMode::Account);
}

void ProfileViewModelTest::offlineShowsTheTypedName()
{
    QCOMPARE(m_model->name(), QStringLiteral("Listener"));
    QCOMPARE(m_model->initials(), QStringLiteral("L"));
    QCOMPARE(m_model->plan(), QString());
    QVERIFY(m_model->avatarUrl().isEmpty());

    QSignalSpy changed(m_model.get(), &ProfileViewModel::changed);
    m_fixture->session.settings.setString(keys::UserDisplayName, QStringLiteral(" Sam Lee "));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_model->name(), QStringLiteral("Sam Lee"));
    QCOMPARE(m_model->initials(), QStringLiteral("SL"));
}

void ProfileViewModelTest::apiKeyModeShowsTheTypedName()
{
    m_fixture->session.settings.setString(keys::UserDisplayName, QStringLiteral("Sam"));
    QVERIFY(m_fixture->modes.setMode(SourceMode::ApiKey));
    QCOMPARE(m_model->name(), QStringLiteral("Sam"));
    QCOMPARE(m_model->plan(), QString());
}

void ProfileViewModelTest::accountModeShowsTheAccount()
{
    m_fixture->session.settings.setString(keys::UserDisplayName, QStringLiteral("Typed"));
    const QByteArray user = "{\"id\":\"owner-a\",\"displayName\":\"Alex Kim\",\"username\":\"alex\","
                            "\"plan\":\"premium\",\"avatarUrl\":\"https://cdn.example.test/a.png\"}";
    QVERIFY(enterAccount(user));
    QCOMPARE(m_model->name(), QStringLiteral("Alex Kim"));
    QCOMPARE(m_model->initials(), QStringLiteral("AK"));
    QCOMPARE(m_model->plan(), QStringLiteral("PREMIUM"));
    QCOMPARE(m_model->avatarUrl(), QUrl(QStringLiteral("https://cdn.example.test/a.png")));

    // Back to Offline: the typed name returns as it was.
    QVERIFY(m_fixture->modes.setMode(SourceMode::LocalOnly));
    QCOMPARE(m_model->name(), QStringLiteral("Typed"));
    QVERIFY(m_model->avatarUrl().isEmpty());
}

void ProfileViewModelTest::unsafePicturesAreNotLoaded()
{
    // A plain http picture fails the address rules; the profile parser
    // drops it first, and the profile card checks again before loading.
    const QByteArray user = "{\"id\":\"owner-a\",\"displayName\":\"Alex\",\"avatarUrl\":\"http://cdn.example.test/a.png\"}";
    QVERIFY(enterAccount(user));
    QVERIFY(m_model->avatarUrl().isEmpty());
}

void ProfileViewModelTest::menuOffersSignOutOnlyWhenSignedIn()
{
    m_model->prepareMenu();
    QCOMPARE(m_model->menu()->count(), 1);
    QCOMPARE(m_model->menu()->idAt(0), QStringLiteral("settings"));
    m_model->triggerMenu(QStringLiteral("settings"));
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Settings);
    // Sign out is refused while signed out.
    m_model->triggerMenu(QStringLiteral("signout"));

    const QByteArray user = "{\"id\":\"owner-a\",\"displayName\":\"Alex\"}";
    QVERIFY(enterAccount(user));
    m_model->prepareMenu();
    QCOMPARE(m_model->menu()->count(), 2);
    QCOMPARE(m_model->menu()->idAt(1), QStringLiteral("signout"));
    m_model->triggerMenu(QStringLiteral("signout"));
    QVERIFY(QTest::qWaitFor([this]() { return m_fixture->modes.mode() == SourceMode::LocalOnly; }, 3000));
    QCOMPARE(m_fixture->session.storedSession(), QStringLiteral("<none>"));
    QCOMPARE(m_model->name(), QStringLiteral("Listener"));
}

void ProfileViewModelTest::initials_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("initials");
    QTest::newRow("one word") << QStringLiteral("listener") << QStringLiteral("L");
    QTest::newRow("two words") << QStringLiteral("sam lee") << QStringLiteral("SL");
    QTest::newRow("three words") << QStringLiteral("Ana Maria Ruiz") << QStringLiteral("AM");
    QTest::newRow("tabs and spaces") << QStringLiteral("  ana\t ruiz ") << QStringLiteral("AR");
    QTest::newRow("empty") << QString() << QStringLiteral("L");
}

void ProfileViewModelTest::initials()
{
    QFETCH(QString, name);
    QFETCH(QString, initials);
    QCOMPARE(ProfileViewModel::initialsOf(name), initials);
}

QTEST_MAIN(ProfileViewModelTest)
#include "tst_profile_view_model.moc"
