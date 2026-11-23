#include "core/cast/CastAudioPlayer.h"
#include "core/cast/CastDiscovery.h"
#include "core/cast/CastMessage.h"
#include "core/cast/CastSession.h"
#include "core/cast/CastTransport.h"
#include "platform/IDnsServiceBrowser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

#include <map>

namespace cast = lmp::core::cast;
using lmp::platform::DnsServiceRecord;
using lmp::platform::IAudioPlayer;

namespace {

// A browser whose answers are set by the test, like the earlier versions'
// fake adapter.
class FakeBrowser final : public lmp::platform::IDnsServiceBrowser {
public:
    class Browse final : public lmp::platform::IDnsBrowseOperation {
    public:
        explicit Browse(QStringList names) : m_names(std::move(names)) {}
        bool wait(int) override { return true; }
        void cancelAndWait() noexcept override {}
        [[nodiscard]] QStringList instances() const override { return m_names; }

    private:
        QStringList m_names;
    };
    class Resolve final : public lmp::platform::IDnsResolveOperation {
    public:
        Resolve(std::optional<DnsServiceRecord> record, bool completes, int* cancels)
            : m_record(std::move(record)), m_completes(completes), m_cancels(cancels)
        {
        }
        bool wait(int) override { return m_completes; }
        void cancelAndWait() noexcept override { ++*m_cancels; }
        [[nodiscard]] std::optional<DnsServiceRecord> record() const override { return m_record; }

    private:
        std::optional<DnsServiceRecord> m_record;
        bool m_completes;
        int* m_cancels;
    };

    std::unique_ptr<lmp::platform::IDnsBrowseOperation> beginBrowse(const QString& serviceName) override
    {
        browsedFor = serviceName;
        if (browseFails) {
            return nullptr;
        }
        return std::make_unique<Browse>(instances);
    }
    std::unique_ptr<lmp::platform::IDnsResolveOperation> beginResolve(const QString& name) override
    {
        if (resolveFails) {
            return nullptr;
        }
        const auto found = records.find(name);
        return std::make_unique<Resolve>(found == records.end() ? std::nullopt : std::optional(found->second),
                                         !timesOut.contains(name), &resolveCancels);
    }

    bool browseFails = false;
    bool resolveFails = false;
    QStringList instances;
    std::map<QString, DnsServiceRecord> records;
    QStringList timesOut;
    QString browsedFor;
    int resolveCancels = 0;
};

DnsServiceRecord record(const QString& instance, const QString& host, const QString& id, const QString& name = {})
{
    DnsServiceRecord result;
    result.instanceName = instance;
    result.host = host;
    result.port = 8009;
    if (!id.isEmpty()) {
        result.properties.insert(QStringLiteral("id"), id);
    }
    if (!name.isEmpty()) {
        result.properties.insert(QStringLiteral("fn"), name);
    }
    return result;
}

// A transport that keeps what is written and is driven by the test.
class FakeTransport final : public cast::ICastTransport {
public:
    void open(const QString& host, quint16 port) override
    {
        openedHost = host;
        openedPort = port;
    }
    void write(const QByteArray& bytes) override { written.append(bytes); }
    void close() override { ++closes; }

    // Every message written so far.
    [[nodiscard]] QList<cast::CastMessage> messages() const
    {
        QList<cast::CastMessage> result;
        cast::CastFrameDecoder decoder;
        for (const QByteArray& bytes : written) {
            decoder.append(bytes);
        }
        bool broken = false;
        while (const std::optional<QByteArray> body = decoder.next(broken)) {
            if (const std::optional<cast::CastMessage> message = cast::decodeCastMessage(*body)) {
                result.append(*message);
            }
        }
        return result;
    }
    // Sends a message from the device.
    void deliver(const char* nameSpace, const QJsonObject& json, const QString& source = QStringLiteral("receiver-0"))
    {
        // The device's frames are built like ours, with its own source.
        QByteArray frame = cast::encodeCastFrame(QString::fromLatin1(nameSpace), QStringLiteral("sender-0"),
                                                 QJsonDocument(json).toJson(QJsonDocument::Compact));
        Q_UNUSED(source)
        emit received(frame);
    }

    QString openedHost;
    quint16 openedPort = 0;
    QList<QByteArray> written;
    int closes = 0;
};

[[nodiscard]] QJsonObject payloadOf(const cast::CastMessage& message)
{
    return QJsonDocument::fromJson(message.payload).object();
}

[[nodiscard]] QJsonObject receiverStatus(const QString& appId, const QString& transportId)
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("RECEIVER_STATUS")},
        {QStringLiteral("status"),
         QJsonObject{{QStringLiteral("applications"),
                      QJsonArray{QJsonObject{{QStringLiteral("appId"), appId},
                                             {QStringLiteral("transportId"), transportId},
                                             {QStringLiteral("sessionId"), QStringLiteral("s1")}}}}}}};
}

[[nodiscard]] QJsonObject mediaStatus(qint64 sessionId, const QString& state, double time, double duration = 0.0,
                                      const QString& idleReason = {})
{
    QJsonObject status{{QStringLiteral("mediaSessionId"), static_cast<double>(sessionId)},
                       {QStringLiteral("playerState"), state},
                       {QStringLiteral("currentTime"), time}};
    if (duration > 0.0) {
        status.insert(QStringLiteral("media"), QJsonObject{{QStringLiteral("duration"), duration}});
    }
    if (!idleReason.isEmpty()) {
        status.insert(QStringLiteral("idleReason"), idleReason);
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("MEDIA_STATUS")},
                       {QStringLiteral("status"), QJsonArray{status}}};
}

struct SessionFixture {
    SessionFixture()
    {
        auto owned = std::make_unique<FakeTransport>();
        transport = owned.get();
        session = std::make_unique<cast::CastSession>(std::move(owned));
    }
    // Starts the session and lets the device report the media app.
    void makeReady()
    {
        session->start(QStringLiteral("192.0.2.10"), 8009);
        emit transport->opened();
        transport->deliver(cast::ReceiverNamespace, receiverStatus(QStringLiteral("CC1AD845"), QStringLiteral("t-7")));
    }

    FakeTransport* transport = nullptr;
    std::unique_ptr<cast::CastSession> session;
};

} // namespace

class CastTest : public QObject {
    Q_OBJECT

private slots:
    // Discovery, as the earlier versions' tests checked it.
    void discoveryFailuresGiveNoDevices()
    {
        FakeBrowser browseFails;
        browseFails.browseFails = true;
        QVERIFY(cast::discoverCastDevices(browseFails, 10).isEmpty());

        FakeBrowser resolveFails;
        resolveFails.instances = {QStringLiteral("Living Room._googlecast._tcp.local")};
        resolveFails.resolveFails = true;
        QVERIFY(cast::discoverCastDevices(resolveFails, 10).isEmpty());

        FakeBrowser noHost;
        noHost.instances = {QStringLiteral("Living Room._googlecast._tcp.local")};
        noHost.records[noHost.instances.first()] = record(noHost.instances.first(), QString(), QStringLiteral("id1"));
        QVERIFY(cast::discoverCastDevices(noHost, 10).isEmpty());
    }

    void discoveryKeepsOrderAndDropsDuplicateIds()
    {
        FakeBrowser browser;
        browser.instances = {QStringLiteral("Living Room._googlecast._tcp.local"),
                             QStringLiteral("Living Room duplicate._googlecast._tcp.local"),
                             QStringLiteral("Kitchen._googlecast._tcp.local")};
        browser.records[browser.instances.at(0)] =
            record(browser.instances.at(0), QStringLiteral("192.0.2.1"), QStringLiteral("a"), QStringLiteral("Living Room"));
        browser.records[browser.instances.at(1)] =
            record(browser.instances.at(1), QStringLiteral("192.0.2.2"), QStringLiteral("a"), QStringLiteral("Again"));
        browser.records[browser.instances.at(2)] =
            record(browser.instances.at(2), QStringLiteral("192.0.2.3"), QStringLiteral("b"));
        const QList<cast::CastDevice> devices = cast::discoverCastDevices(browser, 10);
        QCOMPARE(browser.browsedFor, QStringLiteral("_googlecast._tcp.local"));
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).name, QStringLiteral("Living Room"));
        QCOMPARE(devices.at(0).host, QStringLiteral("192.0.2.1"));
        // Without a friendly name the instance label is used.
        QCOMPARE(devices.at(1).name, QStringLiteral("Kitchen"));
        QCOMPARE(devices.at(1).port, quint16(8009));
        // Every resolve was let go of.
        QCOMPARE(browser.resolveCancels, 3);
    }

    void discoverySkipsResolvesThatTimeOut()
    {
        FakeBrowser browser;
        browser.instances = {QStringLiteral("Slow._googlecast._tcp.local")};
        browser.records[browser.instances.first()] =
            record(browser.instances.first(), QStringLiteral("192.0.2.4"), QStringLiteral("slow"));
        browser.timesOut = browser.instances;
        QVERIFY(cast::discoverCastDevices(browser, 10).isEmpty());
        QCOMPARE(browser.resolveCancels, 1);
    }

    void deviceIdFallsBackToTheInstanceName()
    {
        const std::optional<cast::CastDevice> device =
            cast::castDeviceFrom(record(QStringLiteral("Den._googlecast._tcp.local"), QStringLiteral("den.local"), {}));
        QVERIFY(device);
        QCOMPARE(device->id, QStringLiteral("Den._googlecast._tcp.local"));
        QCOMPARE(device->name, QStringLiteral("Den"));
    }

    // Framing.
    void framesRoundTrip()
    {
        const QByteArray frame = cast::encodeCastFrame(QStringLiteral("urn:x-cast:test"), QStringLiteral("receiver-0"),
                                                       QByteArrayLiteral("{\"type\":\"PING\"}"));
        QCOMPARE(qFromBigEndian<quint32>(frame.constData()), quint32(frame.size() - 4));
        cast::CastFrameDecoder decoder;
        decoder.append(frame.left(3));
        bool broken = false;
        QVERIFY(!decoder.next(broken));
        decoder.append(frame.mid(3));
        const std::optional<QByteArray> body = decoder.next(broken);
        QVERIFY(body);
        const std::optional<cast::CastMessage> message = cast::decodeCastMessage(*body);
        QVERIFY(message);
        QCOMPARE(message->nameSpace, QStringLiteral("urn:x-cast:test"));
        QCOMPARE(message->source, QStringLiteral("sender-0"));
        QCOMPARE(message->destination, QStringLiteral("receiver-0"));
        QCOMPARE(message->payload, QByteArrayLiteral("{\"type\":\"PING\"}"));
    }

    void badFramesAreRefused()
    {
        for (const quint32 length : {quint32(0), quint32(cast::MaxCastMessageBytes + 1)}) {
            QByteArray header(4, '\0');
            qToBigEndian<quint32>(length, header.data());
            cast::CastFrameDecoder decoder;
            decoder.append(header);
            bool broken = false;
            QVERIFY(!decoder.next(broken));
            QVERIFY(broken);
        }
        // A varint that never ends, and a length past the end.
        QVERIFY(!cast::decodeCastMessage(QByteArray(12, '\xff')));
        QVERIFY(!cast::decodeCastMessage(QByteArray("\x22\x7f" "abc", 5)));
    }

    // The session steps.
    void sessionConnectsLaunchesAndOpensTheMediaChannel()
    {
        SessionFixture fixture;
        QSignalSpy ready(fixture.session.get(), &cast::CastSession::ready);
        fixture.session->start(QStringLiteral("192.0.2.10"), 8009);
        QCOMPARE(fixture.transport->openedHost, QStringLiteral("192.0.2.10"));
        // A LOAD before the channel is open waits for it.
        fixture.session->load({.url = QStringLiteral("https://media.invalid/s.mp3"),
                               .title = QStringLiteral("Song"),
                               .artist = QStringLiteral("Artist"),
                               .startAtSeconds = 12.5});
        QVERIFY(fixture.transport->messages().isEmpty());
        emit fixture.transport->opened();
        QList<cast::CastMessage> sent = fixture.transport->messages();
        QCOMPARE(sent.size(), 2);
        QCOMPARE(sent.at(0).nameSpace, QString::fromLatin1(cast::ConnectionNamespace));
        QCOMPARE(payloadOf(sent.at(0)).value(QStringLiteral("type")).toString(), QStringLiteral("CONNECT"));
        QCOMPARE(payloadOf(sent.at(1)).value(QStringLiteral("type")).toString(), QStringLiteral("LAUNCH"));
        QCOMPARE(payloadOf(sent.at(1)).value(QStringLiteral("appId")).toString(), QStringLiteral("CC1AD845"));

        // Another app's status is not the media channel.
        fixture.transport->deliver(cast::ReceiverNamespace, receiverStatus(QStringLiteral("OTHER"), QStringLiteral("x")));
        QCOMPARE(ready.count(), 0);
        fixture.transport->deliver(cast::ReceiverNamespace, receiverStatus(QStringLiteral("CC1AD845"), QStringLiteral("t-7")));
        QCOMPARE(ready.count(), 1);
        sent = fixture.transport->messages();
        QCOMPARE(sent.size(), 4);
        QCOMPARE(sent.at(2).destination, QStringLiteral("t-7"));
        QCOMPARE(payloadOf(sent.at(2)).value(QStringLiteral("type")).toString(), QStringLiteral("CONNECT"));
        const QJsonObject load = payloadOf(sent.at(3));
        QCOMPARE(sent.at(3).nameSpace, QString::fromLatin1(cast::MediaNamespace));
        QCOMPARE(load.value(QStringLiteral("type")).toString(), QStringLiteral("LOAD"));
        QCOMPARE(load.value(QStringLiteral("currentTime")).toDouble(), 12.5);
        const QJsonObject media = load.value(QStringLiteral("media")).toObject();
        QCOMPARE(media.value(QStringLiteral("contentId")).toString(), QStringLiteral("https://media.invalid/s.mp3"));
        QCOMPARE(media.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Song"));
    }

    void sessionAnswersPingsAndSendsHeartbeats()
    {
        SessionFixture fixture;
        fixture.makeReady();
        const qsizetype before = fixture.transport->messages().size();
        fixture.transport->deliver(cast::HeartbeatNamespace, QJsonObject{{QStringLiteral("type"), QStringLiteral("PING")}});
        const QList<cast::CastMessage> sent = fixture.transport->messages();
        QCOMPARE(sent.size(), before + 1);
        QCOMPARE(payloadOf(sent.last()).value(QStringLiteral("type")).toString(), QStringLiteral("PONG"));
        QCOMPARE(cast::CastSession::HeartbeatIntervalMs, 5000);
    }

    void mediaCommandsCarryTheMediaSession()
    {
        SessionFixture fixture;
        fixture.makeReady();
        // Nothing to command before the device names its media session.
        const qsizetype before = fixture.transport->messages().size();
        fixture.session->pause();
        QCOMPARE(fixture.transport->messages().size(), before);

        QSignalSpy statuses(fixture.session.get(), &cast::CastSession::mediaStatus);
        fixture.transport->deliver(cast::MediaNamespace, mediaStatus(4, QStringLiteral("PLAYING"), 3.0, 180.0),
                                   QStringLiteral("t-7"));
        QCOMPARE(statuses.count(), 1);
        fixture.session->pause();
        fixture.session->seek(42.0);
        fixture.session->setVolume(1.7);
        const QList<cast::CastMessage> sent = fixture.transport->messages();
        const QJsonObject pause = payloadOf(sent.at(sent.size() - 3));
        QCOMPARE(pause.value(QStringLiteral("type")).toString(), QStringLiteral("PAUSE"));
        QCOMPARE(pause.value(QStringLiteral("mediaSessionId")).toInteger(), 4);
        const QJsonObject seek = payloadOf(sent.at(sent.size() - 2));
        QCOMPARE(seek.value(QStringLiteral("currentTime")).toDouble(), 42.0);
        const QJsonObject volume = payloadOf(sent.last());
        QCOMPARE(sent.last().destination, QStringLiteral("receiver-0"));
        QCOMPARE(volume.value(QStringLiteral("volume")).toObject().value(QStringLiteral("level")).toDouble(), 1.0);
    }

    void closeFromTheDeviceEndsAReadySession()
    {
        SessionFixture fixture;
        QSignalSpy closed(fixture.session.get(), &cast::CastSession::closed);
        fixture.makeReady();
        fixture.transport->deliver(cast::ConnectionNamespace, QJsonObject{{QStringLiteral("type"), QStringLiteral("CLOSE")}});
        QCOMPARE(closed.count(), 1);
        QVERIFY(!fixture.session->isReady());
    }

    void lostConnectionBeforeReadyFails()
    {
        SessionFixture fixture;
        QSignalSpy failed(fixture.session.get(), &cast::CastSession::failed);
        QSignalSpy closed(fixture.session.get(), &cast::CastSession::closed);
        fixture.session->start(QStringLiteral("192.0.2.10"), 8009);
        emit fixture.transport->closed(QStringLiteral("refused"));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("refused"));
        QCOMPARE(closed.count(), 0);
    }

    // The player contract on a device.
    void castPlayerMapsStatusesToStates()
    {
        SessionFixture fixture;
        fixture.makeReady();
        cast::CastAudioPlayer player;
        player.setSession(fixture.session.get());
        QSignalSpy states(&player, &IAudioPlayer::stateChanged);
        QSignalSpy durations(&player, &IAudioPlayer::durationChanged);
        QSignalSpy positions(&player, &IAudioPlayer::positionChanged);

        const quint64 id = player.open(QUrl(QStringLiteral("https://media.invalid/a.mp3")), true, 0.0);
        QCOMPARE(states.count(), 0); // never from inside open()
        QCoreApplication::processEvents();
        QCOMPARE(states.count(), 1);
        QCOMPARE(states.last().at(1).value<IAudioPlayer::State>(), IAudioPlayer::State::Loading);

        fixture.transport->deliver(cast::MediaNamespace, mediaStatus(9, QStringLiteral("PLAYING"), 1.0, 200.0));
        QCOMPARE(durations.last().at(1).toDouble(), 200.0);
        QCOMPARE(positions.last().at(1).toDouble(), 1.0);
        QCOMPARE(player.state(), IAudioPlayer::State::Playing);
        QCOMPARE(states.last().at(0).toULongLong(), id);

        fixture.transport->deliver(cast::MediaNamespace, mediaStatus(9, QStringLiteral("PAUSED"), 2.0));
        QCOMPARE(player.state(), IAudioPlayer::State::Paused);

        fixture.transport->deliver(cast::MediaNamespace,
                                   mediaStatus(9, QStringLiteral("IDLE"), 0.0, 0.0, QStringLiteral("FINISHED")));
        QCOMPARE(player.state(), IAudioPlayer::State::Ended);
        QCOMPARE(positions.last().at(1).toDouble(), 200.0);
    }

    void castPlayerIgnoresTheEarlierSongsStatus()
    {
        SessionFixture fixture;
        fixture.makeReady();
        cast::CastAudioPlayer player;
        player.setSession(fixture.session.get());
        (void)player.open(QUrl(QStringLiteral("https://media.invalid/a.mp3")), true, 0.0);
        fixture.transport->deliver(cast::MediaNamespace, mediaStatus(1, QStringLiteral("PLAYING"), 5.0, 100.0));
        QCOMPARE(player.state(), IAudioPlayer::State::Playing);

        // The next song: the first song's end arrives late and is dropped.
        (void)player.open(QUrl(QStringLiteral("https://media.invalid/b.mp3")), true, 0.0);
        fixture.transport->deliver(cast::MediaNamespace,
                                   mediaStatus(1, QStringLiteral("IDLE"), 0.0, 0.0, QStringLiteral("INTERRUPTED")));
        fixture.transport->deliver(cast::MediaNamespace,
                                   mediaStatus(1, QStringLiteral("IDLE"), 0.0, 0.0, QStringLiteral("FINISHED")));
        QCOMPARE(player.state(), IAudioPlayer::State::Loading);
        fixture.transport->deliver(cast::MediaNamespace, mediaStatus(2, QStringLiteral("PLAYING"), 0.5, 150.0));
        QCOMPARE(player.state(), IAudioPlayer::State::Playing);
    }

    void castPlayerReportsADeviceError()
    {
        SessionFixture fixture;
        fixture.makeReady();
        cast::CastAudioPlayer player;
        player.setSession(fixture.session.get());
        QSignalSpy errors(&player, &IAudioPlayer::errorOccurred);
        (void)player.open(QUrl(QStringLiteral("https://media.invalid/a.mp3")), true, 0.0);
        fixture.transport->deliver(cast::MediaNamespace,
                                   mediaStatus(3, QStringLiteral("IDLE"), 0.0, 0.0, QStringLiteral("ERROR")));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(player.state(), IAudioPlayer::State::Error);
    }
};

QTEST_GUILESS_MAIN(CastTest)
#include "tst_cast.moc"
