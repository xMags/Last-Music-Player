#include "FakeConnectSocket.h"
#include "PlayerFixture.h"

#include "core/connect/ConnectClient.h"
#include "core/connect/ConnectSync.h"
#include "core/connect/ConnectTracks.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

using lmp::core::PlayerController;
using lmp::core::Track;
using lmp::core::connect::ConnectSync;

namespace {

constexpr auto Self = "this-pc-0001";
constexpr auto Phone = "phone-0001";

[[nodiscard]] Track remoteSong(const QString& id)
{
    Track track;
    track.title = QStringLiteral("Song ") + id;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceTag = QStringLiteral("example-source");
    track.remoteId = id;
    track.sourceUrl = QStringLiteral("https://music.example.test/t/") + id;
    track.durationSeconds = 200.0;
    return track;
}

[[nodiscard]] QJsonObject songJson(const QString& id)
{
    return lmp::core::connect::connectTrackJson(remoteSong(id));
}

[[nodiscard]] QJsonArray devices()
{
    return QJsonArray{QJsonObject{{"deviceId", Self}, {"name", "Windows PC"}, {"kind", "windows"}},
                      QJsonObject{{"deviceId", Phone}, {"name", "Phone"}, {"kind", "android"}}};
}

[[nodiscard]] QJsonObject session(const QString& active, const QJsonObject& state = {}, qint64 ageMs = 0)
{
    return QJsonObject{{"type", "session"},
                       {"session",
                        QJsonObject{{"activeDeviceId", active.isEmpty() ? QJsonValue() : QJsonValue(active)},
                                    {"state", state.isEmpty() ? QJsonValue() : QJsonValue(state)},
                                    {"ageMs", ageMs}}}};
}

[[nodiscard]] QJsonObject phoneState(const QString& id, bool playing, double position,
                                     const QJsonArray& upcoming = {})
{
    return QJsonObject{{"track", songJson(id)}, {"playing", playing}, {"positionSec", position},
                       {"durationSec", 200},     {"volume", 70},       {"shuffle", false},
                       {"repeatMode", 0},        {"upcoming", upcoming}};
}

[[nodiscard]] QJsonObject command(const QJsonObject& body)
{
    return QJsonObject{{"type", "command"}, {"command", body}};
}

[[nodiscard]] QStringList titles(const QList<Track>& tracks)
{
    QStringList list;
    for (const Track& track : tracks) {
        list.append(track.title);
    }
    return list;
}

// A player, the link on a fake socket and the sync between them, linked up
// with nobody playing.
class SyncRig {
public:
    SyncRig()
        : player(fixture.makeController())
        , harness(fixture.clock)
        , sync({.client = *harness.client, .player = *player, .clock = fixture.clock, .castDeviceName = {}})
    {
        harness.client->start();
        settle();
        harness.socket().serverOpen();
        harness.socket().serverSend(
            {{"type", "welcome"}, {"epoch", 1}, {"devices", devices()}, {"session", QJsonObject{}}});
    }

    // Moves time on past the report delay.
    void flush() { fixture.clock.advance(200); }

    // Starts `tracks` playing here.
    void playHere(const QList<Track>& tracks)
    {
        player->playInContext(tracks.constFirst(), tracks);
        settle();
        fixture.audio.start();
    }

    PlayerFixture fixture;
    std::unique_ptr<PlayerController> player;
    ConnectHarness harness;
    ConnectSync sync;
};

} // namespace

class ConnectSyncTest : public QObject {
    Q_OBJECT

private slots:
    void playingHereClaimsTheSession()
    {
        SyncRig rig;
        rig.playHere({remoteSong("a"), remoteSong("b")});
        rig.flush();
        const QList<QJsonObject> states = rig.harness.sentOfType(QStringLiteral("state"));
        QCOMPARE(states.size(), 1);
        const QJsonObject state = states.constFirst().value("state").toObject();
        QVERIFY(state.value("playing").toBool());
        QCOMPARE(state.value("track").toObject().value("id").toString(), QStringLiteral("a"));
        QCOMPARE(state.value("upcoming").toArray().size(), 1);
        QCOMPARE(states.constFirst().value("epoch").toInt(), 1);
    }

    void aRestoredPausedQueueDoesNotClaim()
    {
        SyncRig rig;
        rig.player->restore(lmp::core::QueueEngine::create({remoteSong("a")}, 0), remoteSong("a"), 12.0);
        rig.flush();
        QVERIFY(rig.harness.sentOfType(QStringLiteral("state")).isEmpty());
    }

    void theActiveDeviceReportsChangesNotEchoes()
    {
        SyncRig rig;
        rig.playHere({remoteSong("a")});
        rig.flush();
        rig.harness.socket().serverSend(session(Self, phoneState(QStringLiteral("a"), true, 0)));
        rig.flush();
        QCOMPARE(rig.harness.sentOfType(QStringLiteral("state")).size(), 1);
        // Playing on moves the position as expected: nothing to report.
        rig.fixture.clock.advance(3000);
        rig.fixture.audio.position(3.0);
        rig.flush();
        QCOMPARE(rig.harness.sentOfType(QStringLiteral("state")).size(), 1);
        // A jump is a seek.
        rig.fixture.audio.position(90.0);
        rig.flush();
        QCOMPARE(rig.harness.sentOfType(QStringLiteral("state")).size(), 2);
        // And the heartbeat while playing.
        rig.fixture.clock.advance(15000);
        rig.flush();
        QCOMPARE(rig.harness.sentOfType(QStringLiteral("state")).size(), 3);
    }

    void becomesTheRemoteOfAnotherDevice()
    {
        SyncRig rig;
        rig.playHere({remoteSong("a")});
        rig.flush();
        QSignalSpy changed(&rig.sync, &ConnectSync::remoteChanged);
        const int pausesBefore = rig.fixture.audio.pauseCalls;
        rig.harness.socket().serverSend({{"type", "stop"}, {"epoch", 2}});
        QCOMPARE(rig.fixture.audio.pauseCalls, pausesBefore + 1);
        rig.harness.socket().serverSend(session(
            QString::fromLatin1(Phone),
            phoneState(QStringLiteral("p1"), true, 10.0, QJsonArray{songJson(QStringLiteral("p2"))}), 500));
        QVERIFY(rig.sync.isRemote());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(rig.sync.remote().track->title, QStringLiteral("Song p1"));
        QCOMPARE(titles(rig.sync.remote().upcoming), QStringList{QStringLiteral("Song p2")});
        QCOMPARE(rig.sync.remote().deviceLabel, QStringLiteral("Phone"));
        QCOMPARE(rig.sync.remote().volume, 70);
        QCOMPARE(rig.sync.remotePosition(), 10.5);
        rig.fixture.clock.advance(2000);
        QCOMPARE(rig.sync.remotePosition(), 12.5);
        // This PC's own song stays, paused, and nothing is reported.
        QCOMPARE(rig.player->currentTrack()->remoteId, QStringLiteral("a"));
        rig.flush();
        QCOMPARE(rig.harness.sentOfType(QStringLiteral("state")).size(), 1);
    }

    void remoteControlsSendCommands()
    {
        SyncRig rig;
        rig.harness.socket().serverSend(session(QString::fromLatin1(Phone), phoneState(QStringLiteral("p1"), true, 10.0)));
        QVERIFY(rig.sync.isRemote());
        rig.sync.remoteTogglePlay();
        QCOMPARE(rig.harness.lastSent().value("command").toObject().value("type").toString(), QStringLiteral("pause"));
        QVERIFY(!rig.sync.remote().playing);
        rig.sync.remoteNext();
        QCOMPARE(rig.harness.lastSent().value("command").toObject().value("type").toString(), QStringLiteral("next"));

        // A drag sends one seek once it settles, and wins over a report
        // that crosses it.
        rig.sync.remoteSeek(40.0);
        rig.sync.remoteSeek(50.0);
        rig.fixture.clock.advance(250);
        const QList<QJsonObject> commands = rig.harness.sentOfType(QStringLiteral("command"));
        QCOMPARE(commands.constLast().value("command").toObject().value("type").toString(), QStringLiteral("seek"));
        QCOMPARE(commands.constLast().value("command").toObject().value("positionSec").toDouble(), 50.0);
        QCOMPARE(commands.size(), 3);
        rig.harness.socket().serverSend(session(QString::fromLatin1(Phone), phoneState(QStringLiteral("p1"), false, 10.0)));
        QCOMPARE(rig.sync.remotePosition(), 50.0);
        rig.fixture.clock.advance(2000);
        rig.harness.socket().serverSend(session(QString::fromLatin1(Phone), phoneState(QStringLiteral("p1"), false, 51.0)));
        QCOMPARE(rig.sync.remotePosition(), 51.0);

        rig.sync.remoteSetVolume(20);
        rig.fixture.clock.advance(200);
        QCOMPARE(rig.harness.lastSent().value("command").toObject().value("value").toInt(), 20);
        rig.sync.remoteClearUpcoming();
        const QJsonObject clear = rig.harness.lastSent().value("command").toObject();
        QCOMPARE(clear.value("type").toString(), QStringLiteral("setUpcoming"));
        QVERIFY(clear.value("tracks").toArray().isEmpty());
    }

    void appliesCommandsWhileActive()
    {
        SyncRig rig;
        rig.playHere({remoteSong("a"), remoteSong("b")});
        rig.flush();
        rig.harness.socket().serverSend(session(Self, phoneState(QStringLiteral("a"), true, 0)));
        rig.harness.socket().serverSend(command({{"type", "seek"}, {"positionSec", 30}}));
        QCOMPARE(rig.fixture.audio.seeks.constLast(), 30.0);
        rig.harness.socket().serverSend(command({{"type", "volume"}, {"value", 30}}));
        QCOMPARE(rig.player->volume(), 0.3);
        rig.harness.socket().serverSend(command({{"type", "repeat"}, {"mode", 2}}));
        QCOMPARE(rig.player->repeatMode(), lmp::core::RepeatMode::One);
        rig.harness.socket().serverSend(command({{"type", "pause"}}));
        QCOMPARE(rig.fixture.audio.pauseCalls, 1);
        rig.harness.socket().serverSend(
            command({{"type", "enqueue"}, {"tracks", QJsonArray{songJson(QStringLiteral("n"))}}, {"next", true}}));
        QCOMPARE(visibleTitles(*rig.player), (QStringList{QStringLiteral("Song n"), QStringLiteral("Song b")}));
    }

    void setUpcomingKeepsSongsTheRemoteNeverSaw()
    {
        SyncRig rig;
        rig.playHere({remoteSong("a"), remoteSong("b"), remoteSong("c")});
        rig.flush();
        rig.harness.socket().serverSend(session(Self, phoneState(QStringLiteral("a"), true, 0)));
        // Added after the last report: the remote has not seen it.
        rig.player->addToQueue({remoteSong("d")});
        rig.harness.socket().serverSend(
            command({{"type", "setUpcoming"}, {"tracks", QJsonArray{songJson(QStringLiteral("c"))}}}));
        QCOMPARE(visibleTitles(*rig.player), (QStringList{QStringLiteral("Song c"), QStringLiteral("Song d")}));
        // An empty list is Clear.
        rig.harness.socket().serverSend(command({{"type", "setUpcoming"}, {"tracks", QJsonArray{}}}));
        QVERIFY(visibleTitles(*rig.player).isEmpty());
    }

    void takesOverWhatAnotherDeviceHandsOver()
    {
        SyncRig rig;
        rig.harness.socket().serverSend(session(QString::fromLatin1(Phone), phoneState(QStringLiteral("p1"), true, 10.0)));
        QVERIFY(rig.sync.isRemote());
        rig.harness.socket().serverSend(session(Self, {}));
        rig.harness.socket().serverSend(
            {{"type", "takeover"},
             {"epoch", 4},
             {"state", phoneState(QStringLiteral("p1"), true, 30.0, QJsonArray{songJson(QStringLiteral("p2"))})},
             {"play", true}});
        settle();
        QVERIFY(!rig.sync.isRemote());
        QCOMPARE(rig.player->currentTrack()->remoteId, QStringLiteral("p1"));
        QCOMPARE(rig.fixture.audio.opens.constLast().startAtSeconds, 30.0);
        QVERIFY(rig.fixture.audio.opens.constLast().autoplay);
        QCOMPARE(visibleTitles(*rig.player), QStringList{QStringLiteral("Song p2")});
        rig.fixture.audio.start();
        rig.flush();
        const QList<QJsonObject> states = rig.harness.sentOfType(QStringLiteral("state"));
        QVERIFY(!states.isEmpty());
        QCOMPARE(states.constLast().value("epoch").toInt(), 4);
    }

    void keepsWhatPlayedWhenTheOtherDeviceGoes()
    {
        SyncRig rig;
        rig.harness.socket().serverSend(session(
            QString::fromLatin1(Phone),
            phoneState(QStringLiteral("p1"), false, 42.0, QJsonArray{songJson(QStringLiteral("p2"))})));
        QVERIFY(rig.sync.isRemote());
        rig.harness.socket().serverSend(session(QString(), phoneState(QStringLiteral("p1"), false, 42.0)));
        settle();
        QVERIFY(!rig.sync.isRemote());
        QCOMPARE(rig.player->currentTrack()->remoteId, QStringLiteral("p1"));
        QCOMPARE(rig.fixture.audio.opens.constLast().startAtSeconds, 42.0);
        QVERIFY(!rig.fixture.audio.opens.constLast().autoplay);
        QCOMPARE(visibleTitles(*rig.player), QStringList{QStringLiteral("Song p2")});
        // Paused: no claim.
        rig.fixture.audio.ready();
        rig.flush();
        QVERIFY(rig.harness.sentOfType(QStringLiteral("state")).isEmpty());
    }

    void playOnReportsAPausedSessionFirst()
    {
        SyncRig rig;
        rig.player->restore(lmp::core::QueueEngine::create({remoteSong("a")}, 0), remoteSong("a"), 12.0);
        rig.sync.playOn(QString::fromLatin1(Phone));
        const QList<QByteArray>& sent = rig.harness.log->sent;
        QVERIFY(sent.size() >= 2);
        const QJsonObject state = QJsonDocument::fromJson(sent.at(sent.size() - 2)).object();
        QCOMPARE(state.value("type").toString(), QStringLiteral("state"));
        QVERIFY(!state.value("state").toObject().value("playing").toBool());
        const QJsonObject transfer = rig.harness.lastSent();
        QCOMPARE(transfer.value("type").toString(), QStringLiteral("transfer"));
        QCOMPARE(transfer.value("to").toString(), QString::fromLatin1(Phone));
    }

    void playOnWithNothingToMoveSaysSo()
    {
        SyncRig rig;
        QSignalSpy notices(&rig.sync, &ConnectSync::notice);
        rig.sync.playOn(QString::fromLatin1(Phone));
        QCOMPARE(notices.count(), 1);
        QCOMPARE(rig.harness.lastSent().value("type").toString(), QStringLiteral("hello"));
    }

    void filesOnThisPcNeverLeaveIt()
    {
        SyncRig rig;
        rig.playHere(rig.fixture.songs({QStringLiteral("private-one")}));
        rig.flush();
        const QList<QJsonObject> states = rig.harness.sentOfType(QStringLiteral("state"));
        QCOMPARE(states.size(), 1);
        const QByteArray text = QJsonDocument(states.constFirst()).toJson();
        QVERIFY(!text.contains(".mp3"));
        QVERIFY(states.constFirst().value("state").toObject().value("track").toObject().value("unavailable").toBool());
    }
};

QTEST_GUILESS_MAIN(ConnectSyncTest)
#include "tst_connect_sync.moc"
