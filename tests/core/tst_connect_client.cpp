#include "FakeConnectSocket.h"
#include "FakePlaybackClock.h"

#include "core/connect/ConnectClient.h"
#include "core/connect/ConnectProtocol.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using lmp::core::connect::Command;
using lmp::core::connect::ConnectClient;

namespace {

void settle()
{
    QCoreApplication::processEvents();
}

[[nodiscard]] QJsonObject device(const QString& id, const QString& name, const QString& kind = QStringLiteral("android"))
{
    return QJsonObject{{"deviceId", id}, {"name", name}, {"kind", kind}, {"canPlay", true}, {"active", false}};
}

[[nodiscard]] QJsonObject welcome(const QString& activeDeviceId, qint64 epoch = 3)
{
    return QJsonObject{
        {"type", "welcome"},
        {"epoch", epoch},
        {"devices", QJsonArray{device(QStringLiteral("this-pc-0001"), QStringLiteral("Windows PC"), QStringLiteral("windows")),
                               device(QStringLiteral("phone-0001"), QStringLiteral("Phone"))}},
        {"session", QJsonObject{{"activeDeviceId", activeDeviceId.isEmpty() ? QJsonValue() : QJsonValue(activeDeviceId)},
                                {"state", QJsonValue()},
                                {"ageMs", 0}}}};
}

// Starts the client and opens its socket: ticket, socket, hello.
void link(ConnectHarness& harness)
{
    harness.client->start();
    settle();
    QVERIFY(harness.log->current != nullptr);
    harness.socket().serverOpen();
}

} // namespace

class ConnectClientTest : public QObject {
    Q_OBJECT

private slots:
    void opensWithTicketAndSaysHello()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        harness.client->start();
        QCOMPARE(harness.client->status(), ConnectClient::Status::Connecting);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        QCOMPARE(harness.log->opened.size(), 1);
        QCOMPARE(harness.log->opened.constFirst().toString(QUrl::FullyEncoded),
                 QStringLiteral("wss://api.example.test/v1/music/connect?ticket=tick-1"));
        harness.socket().serverOpen();
        const QJsonObject hello = harness.lastSent();
        QCOMPARE(hello.value("type").toString(), QStringLiteral("hello"));
        QCOMPARE(hello.value("deviceId").toString(), QStringLiteral("this-pc-0001"));
        QCOMPARE(hello.value("kind").toString(), QStringLiteral("windows"));
        QCOMPARE(harness.client->status(), ConnectClient::Status::Connecting);

        harness.socket().serverSend(welcome(QStringLiteral("phone-0001")));
        QCOMPARE(harness.client->status(), ConnectClient::Status::Open);
        QCOMPARE(harness.client->devices().size(), 2);
        QVERIFY(harness.client->isRemote());
        QVERIFY(!harness.client->isActive());
        QCOMPARE(harness.client->activeDevice()->name, QStringLiteral("Phone"));
        QCOMPARE(harness.client->epoch(), 3);

        // A second start while linked does nothing.
        harness.client->start();
        settle();
        QCOMPARE(harness.ticketCalls, 1);
    }

    void replacesAnInvalidDeviceId()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock, QStringLiteral("bad"));
        QVERIFY(lmp::core::connect::isValidDeviceId(harness.client->deviceId()));
    }

    void reportsWithTheLatestEpoch()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverSend(welcome(QString(), 3));
        harness.socket().serverSend({{"type", "session"}, {"epoch", 9}, {"session", QJsonObject{}}});
        QVERIFY(harness.client->sendState({}));
        QCOMPARE(harness.lastSent().value("epoch").toInt(), 9);
        harness.socket().serverSend({{"type", "stop"}, {"epoch", 11}});
        QVERIFY(harness.client->sendState({}));
        QCOMPARE(harness.lastSent().value("epoch").toInt(), 11);
    }

    void passesCommandsOnlyWhileActive()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        QSignalSpy commands(harness.client.get(), &ConnectClient::commandReceived);
        harness.socket().serverSend(welcome(QStringLiteral("phone-0001")));
        harness.socket().serverSend({{"type", "command"}, {"command", QJsonObject{{"type", "next"}}}});
        QCOMPARE(commands.count(), 0);
        harness.socket().serverSend(
            {{"type", "session"}, {"session", QJsonObject{{"activeDeviceId", "this-pc-0001"}}}});
        QVERIFY(harness.client->isActive());
        harness.socket().serverSend({{"type", "command"}, {"command", QJsonObject{{"type", "next"}}}});
        QCOMPARE(commands.count(), 1);
        QCOMPARE(commands.constFirst().constFirst().value<Command>().type, Command::Type::Next);
    }

    void reconnectsWithGrowingWaits()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverClose(1006);
        QCOMPARE(harness.client->status(), ConnectClient::Status::Off);
        clock.advance(999);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        clock.advance(1);
        settle();
        QCOMPARE(harness.ticketCalls, 2);
        // It never opened: the next wait is twice as long.
        harness.socket().serverClose(1006);
        clock.advance(1999);
        settle();
        QCOMPARE(harness.ticketCalls, 2);
        clock.advance(1);
        settle();
        QCOMPARE(harness.ticketCalls, 3);
    }

    void aLastingConnectionStartsTheWaitsOver()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverClose(1006);
        clock.advance(1000);
        settle();
        harness.socket().serverOpen();
        // Keepalives every 25 s keep the watchdog quiet for over a minute.
        for (int step = 0; step < 3; ++step) {
            clock.advance(25000);
            harness.socket().serverSend({{"type", "keepalive"}});
        }
        harness.socket().serverClose(1006);
        clock.advance(1000);
        settle();
        QCOMPARE(harness.ticketCalls, 3);
    }

    void doesNotFightAReplacementOrASignOut_data()
    {
        QTest::addColumn<int>("code");
        QTest::newRow("replaced") << lmp::core::connect::CloseReplaced;
        QTest::newRow("signed out") << lmp::core::connect::CloseSignedOut;
    }

    void doesNotFightAReplacementOrASignOut()
    {
        QFETCH(int, code);
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverClose(code);
        clock.advance(120000);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        harness.client->start();
        settle();
        QCOMPARE(harness.ticketCalls, 2);
    }

    void floodingWaitsHalfAMinute()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverClose(lmp::core::connect::CloseFlooding);
        clock.advance(29999);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        clock.advance(1);
        settle();
        QCOMPARE(harness.ticketCalls, 2);
    }

    void givesUpASilentSocket()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverSend(welcome(QStringLiteral("phone-0001")));
        QSignalSpy sessions(harness.client.get(), &ConnectClient::sessionChanged);
        clock.advance(59999);
        QCOMPARE(harness.client->status(), ConnectClient::Status::Open);
        clock.advance(1);
        QCOMPARE(harness.log->closeCodes.constLast(), lmp::core::connect::CloseSilent);
        QCOMPARE(harness.client->status(), ConnectClient::Status::Off);
        QVERIFY(!harness.client->isRemote());
        QCOMPARE(sessions.count(), 1);
        clock.advance(1000);
        settle();
        QCOMPARE(harness.ticketCalls, 2);
    }

    void aFailedTicketIsTriedAgain()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        harness.ticketFails = true;
        harness.client->start();
        settle();
        QCOMPARE(harness.log->created, 0);
        QCOMPARE(harness.client->status(), ConnectClient::Status::Off);
        harness.ticketFails = false;
        clock.advance(1000);
        settle();
        QCOMPARE(harness.log->created, 1);
    }

    void aTicketRefusalWaitsAsLongAsTheServerAsks()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        harness.ticketFails = true;
        harness.ticketRetryAfterSeconds = 45;
        harness.client->start();
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        clock.advance(44999);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
        clock.advance(1);
        settle();
        QCOMPARE(harness.ticketCalls, 2);
    }

    void stopReleasesTheSessionAndStaysDown()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverSend(welcome(QStringLiteral("this-pc-0001")));
        QVERIFY(harness.client->isActive());
        harness.client->stop();
        QCOMPARE(harness.lastSent().value("type").toString(), QStringLiteral("release"));
        QCOMPARE(harness.client->status(), ConnectClient::Status::Off);
        QVERIFY(harness.client->session().activeDeviceId.isEmpty());
        QVERIFY(harness.client->devices().isEmpty());
        QVERIFY(!harness.client->sendState({}));
        clock.advance(120000);
        settle();
        QCOMPARE(harness.ticketCalls, 1);
    }

    void aTicketAfterStopIsDropped()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        harness.holdTicket = true;
        harness.client->start();
        harness.client->stop();
        harness.releaseTicket();
        settle();
        QCOMPARE(harness.log->created, 0);
        QCOMPARE(harness.client->status(), ConnectClient::Status::Off);
    }

    void errorsAndTakeoversReachTheirListeners()
    {
        FakePlaybackClock clock;
        ConnectHarness harness(clock);
        link(harness);
        harness.socket().serverSend(welcome(QString()));
        QSignalSpy errors(harness.client.get(), &ConnectClient::errorReceived);
        QSignalSpy takeovers(harness.client.get(), &ConnectClient::takeoverReceived);
        QSignalSpy stops(harness.client.get(), &ConnectClient::stopReceived);
        harness.socket().serverSend({{"type", "error"}, {"code", "nothing_to_transfer"}});
        harness.socket().serverSend(
            {{"type", "takeover"}, {"epoch", 5}, {"state", QJsonObject{{"positionSec", 3}}}, {"play", true}});
        harness.socket().serverSend({{"type", "stop"}, {"epoch", 6}});
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.constFirst().constFirst().toString(), QStringLiteral("nothing_to_transfer"));
        QCOMPARE(takeovers.count(), 1);
        QCOMPARE(stops.count(), 1);
        QCOMPARE(harness.client->epoch(), 6);
    }
};

QTEST_GUILESS_MAIN(ConnectClientTest)
#include "tst_connect_client.moc"
