#include "core/connect/ConnectProtocol.h"
#include "core/connect/ConnectTracks.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

namespace connect = lmp::core::connect;
using connect::Command;
using connect::ServerMessage;

namespace {

[[nodiscard]] QByteArray json(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

[[nodiscard]] QJsonObject parse(const QByteArray& text)
{
    return QJsonDocument::fromJson(text).object();
}

[[nodiscard]] QJsonObject song(const QString& id)
{
    return QJsonObject{{"id", id},
                       {"title", "Song " + id},
                       {"artist", "Artist"},
                       {"sourceKind", "remote"},
                       {"provider", "example-source"},
                       {"sourceUrl", "https://music.example.test/t/" + id},
                       {"durationSec", 200}};
}

} // namespace

class ConnectProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void readsWelcome()
    {
        const QJsonObject state{{"track", song("a")},
                                {"playing", true},
                                {"positionSec", 42.5},
                                {"durationSec", 268},
                                {"volume", 62},
                                {"shuffle", false},
                                {"repeatMode", 1},
                                {"upcoming", QJsonArray{song("b"), song("c")}},
                                {"castDevice", "Living Room"}};
        const ServerMessage message = connect::parseServerMessage(json(
            {{"type", "welcome"},
             {"deviceId", "this-pc-0001"},
             {"epoch", 3},
             {"devices",
              QJsonArray{QJsonObject{{"deviceId", "phone-0001"}, {"name", "Phone"}, {"kind", "android"}, {"active", true}},
                         QJsonObject{{"deviceId", "tab-0001"}, {"name", "Chrome on Mac"}, {"kind", "web"}, {"canPlay", false}},
                         QJsonObject{{"name", "no id"}}}},
             {"session", QJsonObject{{"activeDeviceId", "phone-0001"}, {"state", state}, {"ageMs", 120}}}}));
        QCOMPARE(message.type, ServerMessage::Type::Welcome);
        QCOMPARE(message.epoch, std::optional<qint64>(3));
        QCOMPARE(message.devices.size(), 2);
        QCOMPARE(message.devices.at(0).kind, connect::DeviceKind::Android);
        QVERIFY(message.devices.at(0).active);
        QVERIFY(message.devices.at(0).canPlay);
        QVERIFY(!message.devices.at(1).canPlay);
        QCOMPARE(message.session.activeDeviceId, QStringLiteral("phone-0001"));
        QCOMPARE(message.session.ageMs, 120);
        QVERIFY(message.session.state.has_value());
        QCOMPARE(message.session.state->positionSeconds, 42.5);
        QCOMPARE(message.session.state->volume, 62);
        QCOMPARE(message.session.state->repeatMode, 1);
        QCOMPARE(message.session.state->upcoming.size(), 2);
        QCOMPARE(message.session.state->castDevice, QStringLiteral("Living Room"));
    }

    void clampsAndDefaultsPlayback()
    {
        const std::optional<connect::Playback> state = connect::parsePlayback(
            {{"playing", "yes"}, {"positionSec", -5}, {"volume", 250}, {"repeatMode", 7}, {"upcoming", "none"}});
        QVERIFY(state.has_value());
        QVERIFY(!state->playing);
        QCOMPARE(state->positionSeconds, 0.0);
        QCOMPARE(state->volume, 100);
        QCOMPARE(state->repeatMode, 0);
        QVERIFY(state->upcoming.isEmpty());
        QVERIFY(state->track.isEmpty());
    }

    void ignoresWhatItDoesNotKnow()
    {
        QCOMPARE(connect::parseServerMessage("not json").type, ServerMessage::Type::Ignored);
        QCOMPARE(connect::parseServerMessage("[1,2]").type, ServerMessage::Type::Ignored);
        QCOMPARE(connect::parseServerMessage(json({{"type", "keepalive"}})).type, ServerMessage::Type::Ignored);
        // A command that is not well formed is not a command.
        QCOMPARE(connect::parseServerMessage(json({{"type", "command"}, {"command", QJsonObject{{"type", "dance"}}}})).type,
                 ServerMessage::Type::Ignored);
        // Over the size limit: not read at all.
        QByteArray huge = json({{"type", "stop"}});
        huge.append(QByteArray(connect::MaxMessageBytes, ' '));
        QCOMPARE(connect::parseServerMessage(huge).type, ServerMessage::Type::Ignored);
    }

    void readsTakeoverStopAndError()
    {
        const ServerMessage takeover = connect::parseServerMessage(
            json({{"type", "takeover"}, {"epoch", 4}, {"state", QJsonObject{{"track", song("a")}}}, {"play", false}}));
        QCOMPARE(takeover.type, ServerMessage::Type::Takeover);
        QVERIFY(!takeover.play);
        QCOMPARE(takeover.epoch, std::optional<qint64>(4));
        const ServerMessage stop = connect::parseServerMessage(json({{"type", "stop"}, {"epoch", 5}}));
        QCOMPARE(stop.type, ServerMessage::Type::Stop);
        const ServerMessage error = connect::parseServerMessage(
            json({{"type", "error"}, {"code", "unknown_device"}, {"message", "gone"}}));
        QCOMPARE(error.type, ServerMessage::Type::Error);
        QCOMPARE(error.errorCode, QStringLiteral("unknown_device"));
    }

    void readsCommands_data()
    {
        QTest::addColumn<QJsonObject>("object");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("value");
        QTest::newRow("play") << QJsonObject{{"type", "play"}} << true << int(Command::Type::Play) << 0;
        QTest::newRow("volume clamped") << QJsonObject{{"type", "volume"}, {"value", 140}} << true
                                        << int(Command::Type::Volume) << 100;
        QTest::newRow("volume without value") << QJsonObject{{"type", "volume"}} << false << 0 << 0;
        QTest::newRow("seek without number") << QJsonObject{{"type", "seek"}, {"positionSec", "x"}} << false << 0 << 0;
        QTest::newRow("repeat 2") << QJsonObject{{"type", "repeat"}, {"mode", 2}} << true << int(Command::Type::Repeat) << 2;
        QTest::newRow("repeat 3") << QJsonObject{{"type", "repeat"}, {"mode", 3}} << false << 0 << 0;
        QTest::newRow("play upcoming") << QJsonObject{{"type", "playUpcoming"}, {"position", 2}} << true
                                       << int(Command::Type::PlayUpcoming) << 2;
        QTest::newRow("play upcoming fraction") << QJsonObject{{"type", "playUpcoming"}, {"position", 1.5}} << false << 0
                                                << 0;
        QTest::newRow("play tracks index clamped")
            << QJsonObject{{"type", "playTracks"}, {"tracks", QJsonArray{song("a"), song("b")}}, {"index", 9}} << true
            << int(Command::Type::PlayTracks) << 1;
        QTest::newRow("play no tracks") << QJsonObject{{"type", "playTracks"}, {"tracks", QJsonArray{}}} << false << 0 << 0;
        QTest::newRow("clear upcoming") << QJsonObject{{"type", "setUpcoming"}, {"tracks", QJsonArray{}}} << true
                                        << int(Command::Type::SetUpcoming) << 0;
        QTest::newRow("enqueue nothing") << QJsonObject{{"type", "enqueue"}, {"tracks", QJsonArray{}}} << false << 0 << 0;
    }

    void readsCommands()
    {
        QFETCH(QJsonObject, object);
        QFETCH(bool, valid);
        QFETCH(int, type);
        QFETCH(int, value);
        const std::optional<Command> command = connect::parseCommand(object);
        QCOMPARE(command.has_value(), valid);
        if (command) {
            QCOMPARE(int(command->type), type);
            QCOMPARE(command->value, value);
        }
    }

    void commandsRoundTrip()
    {
        Command command;
        command.type = Command::Type::Enqueue;
        command.tracks = {song("a")};
        command.enabled = true;
        const QJsonObject sent = parse(connect::commandMessage(command));
        QCOMPARE(sent.value("type").toString(), QStringLiteral("command"));
        const std::optional<Command> back = connect::parseCommand(sent.value("command").toObject());
        QVERIFY(back.has_value());
        QCOMPARE(*back, command);
    }

    void writesHelloAndState()
    {
        const QJsonObject hello = parse(connect::helloMessage(QStringLiteral("this-pc-0001"), QStringLiteral("  ")));
        QCOMPARE(hello.value("type").toString(), QStringLiteral("hello"));
        QCOMPARE(hello.value("protocol").toInt(), 1);
        QCOMPARE(hello.value("kind").toString(), QStringLiteral("windows"));
        QCOMPARE(hello.value("name").toString(), QStringLiteral("Windows PC"));
        QVERIFY(hello.value("canPlay").toBool());

        connect::Playback state;
        state.volume = 40;
        const QJsonObject message = parse(connect::stateMessage(7, state));
        QCOMPARE(message.value("epoch").toInt(), 7);
        const QJsonObject body = message.value("state").toObject();
        QVERIFY(body.value("track").isNull());
        QVERIFY(body.value("castDevice").isNull());
        QCOMPARE(body.value("volume").toInt(), 40);

        state.upcoming = QList<QJsonObject>(connect::UpcomingLimit + 20, song("x"));
        QCOMPARE(connect::playbackJson(state).value("upcoming").toArray().size(), connect::UpcomingLimit);
    }

    void reconnectDelaysGrowToThirtySeconds()
    {
        using std::chrono::milliseconds;
        QCOMPARE(connect::reconnectDelay(0, 0.5), milliseconds(1000));
        QCOMPARE(connect::reconnectDelay(1, 0.5), milliseconds(2000));
        QCOMPARE(connect::reconnectDelay(4, 0.5), milliseconds(16000));
        QCOMPARE(connect::reconnectDelay(5, 0.5), milliseconds(30000));
        QCOMPARE(connect::reconnectDelay(40, 0.5), milliseconds(30000));
        // Jitter: 0.75 to 1.25 of the base.
        QCOMPARE(connect::reconnectDelay(0, 0.0), milliseconds(750));
        QCOMPARE(connect::reconnectDelay(0, 0.999999), milliseconds(1250));
        QCOMPARE(connect::reconnectDelay(0, 0.5, connect::MaxBackoff), milliseconds(30000));
    }

    void buildsTheSocketAddress()
    {
        const std::optional<QUrl> url = connect::socketUrl(QStringLiteral("https://api.example.test"),
                                                           QStringLiteral("/v1/music/connect"), QStringLiteral("a+b/c"));
        QVERIFY(url.has_value());
        QCOMPARE(url->toString(QUrl::FullyEncoded),
                 QStringLiteral("wss://api.example.test/v1/music/connect?ticket=a%2Bb%2Fc"));
        QCOMPARE(connect::socketUrl(QStringLiteral("http://127.0.0.1:8080"), QStringLiteral("/v1/music/connect"),
                                    QStringLiteral("t"))
                     ->scheme(),
                 QStringLiteral("ws"));
        QVERIFY(!connect::socketUrl(QStringLiteral("https://api.example.test"), QStringLiteral("//evil.test/v1/x"),
                                    QStringLiteral("t")));
        QVERIFY(!connect::socketUrl(QStringLiteral("https://api.example.test"), QStringLiteral("/v1/a?x=1"),
                                    QStringLiteral("t")));
        QVERIFY(!connect::socketUrl(QStringLiteral("https://api.example.test"), QStringLiteral("/other"),
                                    QStringLiteral("t")));
        QVERIFY(!connect::socketUrl(QStringLiteral("ftp://api.example.test"), QStringLiteral("/v1/music/connect"),
                                    QStringLiteral("t")));
        QVERIFY(!connect::socketUrl(QStringLiteral("https://api.example.test"), QStringLiteral("/v1/music/connect"),
                                    QString()));
    }

    void movesTheRemotePositionOn()
    {
        connect::Playback state;
        state.positionSeconds = 10.0;
        state.durationSeconds = 12.0;
        QCOMPARE(connect::positionAt(state, 500, 500), 10.0); // paused
        state.playing = true;
        QCOMPARE(connect::positionAt(state, 500, 500), 11.0);
        QCOMPARE(connect::positionAt(state, 5000, 0), 12.0); // never past the end
    }

    void deviceIds()
    {
        QVERIFY(connect::isValidDeviceId(QStringLiteral("abc_DEF-12")));
        QVERIFY(!connect::isValidDeviceId(QStringLiteral("short")));
        QVERIFY(!connect::isValidDeviceId(QStringLiteral("has space 123")));
        const QString id = connect::newDeviceId();
        QVERIFY(connect::isValidDeviceId(id));
        QVERIFY(id != connect::newDeviceId());
    }

    void songsTravelWithoutPaths()
    {
        lmp::core::Track file;
        file.title = QStringLiteral("Home Recording");
        file.artist = QStringLiteral("Me");
        file.sourceKind = QStringLiteral("local");
        file.filePath = QStringLiteral("C:\\Music\\private\\home.mp3");
        file.durationSeconds = 61;
        const QJsonObject object = connect::connectTrackJson(file);
        QVERIFY(!QJsonDocument(object).toJson().contains("private"));
        QVERIFY(object.value("unavailable").toBool());
        QVERIFY(object.value("id").toString().startsWith(QStringLiteral("lmp-")));
        const lmp::core::Track back = connect::trackFromConnect(object);
        QVERIFY(back.unavailable);
        QCOMPARE(back.title, file.title);
        QCOMPARE(back.durationText, QStringLiteral("1:01"));

        const lmp::core::Track remote = connect::trackFromConnect(song("r1"));
        QVERIFY(!remote.unavailable);
        QCOMPARE(remote.remoteId, QStringLiteral("r1"));
        QCOMPARE(remote.sourceUrl, QStringLiteral("https://music.example.test/t/r1"));
        QCOMPARE(connect::connectTrackId(connect::connectTrackJson(remote)), QStringLiteral("r1"));
    }
};

QTEST_GUILESS_MAIN(ConnectProtocolTest)
#include "tst_connect_protocol.moc"
