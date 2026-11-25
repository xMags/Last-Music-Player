#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "../core/FakePlaybackClock.h"
#include "../core/ModeServiceFixture.h"
#include "core/account/MusicSyncService.h"
#include "core/db/AccountOutboxRepository.h"
#include "ui/welcome/WelcomeViewModel.h"

using lmp::core::account::MusicSyncService;
using lmp::test::FakeHttpTransport;
using lmp::ui::WelcomeViewModel;

namespace {

void settle()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

struct WelcomeFixture {
    WelcomeFixture()
        : outbox(modes.library.database())
        , sync({.transport = modes.session.client,
                .session = modes.session.service,
                .mode = modes.modes,
                .accounts = modes.accounts,
                .outbox = outbox,
                .settings = modes.session.settings,
                .gate = modes.session.gate,
                .clock = clock,
                .nowUtc = [] { return QStringLiteral("2026-11-25T10:00:00Z"); }})
        , welcome({.client = modes.session.client,
                   .session = modes.session.service,
                   .sync = sync,
                   .accounts = modes.accounts,
                   .settings = modes.session.settings})
    {
    }

    lmp::test::ModeServiceFixture modes;
    lmp::core::db::AccountOutboxRepository outbox;
    FakePlaybackClock clock;
    MusicSyncService sync;
    WelcomeViewModel welcome;
};

} // namespace

// The welcome flow and the account that opened it.
class WelcomeViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void signingOutClosesTheFirstRunFlow();
    void anotherAccountClosesIt();
};

void WelcomeViewModelTest::signingOutClosesTheFirstRunFlow()
{
    WelcomeFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    settle();
    fixture.welcome.open(true);
    QVERIFY(fixture.welcome.visible());
    QVERIFY(!fixture.welcome.closable());
    QVERIFY(fixture.welcome.busy()); // the languages are on their way

    QSignalSpy visible(&fixture.welcome, &WelcomeViewModel::visibleChanged);
    QVERIFY(fixture.modes.session.service.removeLocalSession());
    settle();
    QVERIFY(!fixture.welcome.visible());
    QVERIFY(!fixture.welcome.busy());
    QCOMPARE(visible.count(), 1);

    // The languages that answer afterwards change nothing.
    (void)fixture.modes.session.transport.answer(QStringLiteral("/v1/music/languages"),
                                                 FakeHttpTransport::ok("{\"languages\":[]}"));
    settle();
    QVERIFY(!fixture.welcome.visible());
    QVERIFY(!fixture.welcome.busy());
}

void WelcomeViewModelTest::anotherAccountClosesIt()
{
    WelcomeFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    settle();
    fixture.welcome.open(true);
    QVERIFY(fixture.welcome.visible());
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-b")));
    settle();
    QVERIFY(!fixture.welcome.visible());
}

QTEST_GUILESS_MAIN(WelcomeViewModelTest)
#include "tst_welcome_view_model.moc"
