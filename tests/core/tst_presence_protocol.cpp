#include "core/presence/PresenceProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QtEndian>

using lmp::core::presence::FrameDecoder;
using lmp::core::presence::Opcode;
using lmp::core::presence::PresencePayload;

namespace presence = lmp::core::presence;

class PresenceProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void encodesLittleEndianHeader()
    {
        const QByteArray frame = presence::encodeFrame(Opcode::Frame, QByteArrayLiteral("abc"));
        QCOMPARE(frame.size(), 11);
        QCOMPARE(frame.left(8), QByteArray("\x01\x00\x00\x00\x03\x00\x00\x00", 8));
        QCOMPARE(frame.mid(8), QByteArrayLiteral("abc"));
    }

    void decodesFramesSplitAcrossReads()
    {
        const QByteArray bytes = presence::encodeFrame(Opcode::Ping, QByteArrayLiteral("{\"a\":1}"))
            + presence::encodeFrame(Opcode::Frame, QByteArrayLiteral("{}"));
        FrameDecoder decoder;
        bool broken = true;
        decoder.append(bytes.left(5));
        QVERIFY(!decoder.next(broken));
        QVERIFY(!broken);
        decoder.append(bytes.mid(5, 8));
        QVERIFY(!decoder.next(broken));
        decoder.append(bytes.mid(13));
        const auto first = decoder.next(broken);
        QVERIFY(first);
        QCOMPARE(first->opcode, static_cast<qint32>(Opcode::Ping));
        QCOMPARE(first->payload, QByteArrayLiteral("{\"a\":1}"));
        const auto second = decoder.next(broken);
        QVERIFY(second);
        QCOMPARE(second->opcode, static_cast<qint32>(Opcode::Frame));
        QCOMPARE(second->payload, QByteArrayLiteral("{}"));
        QVERIFY(!decoder.next(broken));
        QVERIFY(!broken);
    }

    void refusesOversizedAndNegativeLengths()
    {
        for (const qint32 length : {presence::MaxFrameBytes + 1, -1}) {
            QByteArray header(8, '\0');
            qToLittleEndian<qint32>(1, header.data());
            qToLittleEndian<qint32>(length, header.data() + 4);
            FrameDecoder decoder;
            decoder.append(header);
            bool broken = false;
            QVERIFY(!decoder.next(broken));
            QVERIFY(broken);
        }
    }

    void handshakeCarriesClientId()
    {
        const QJsonObject json = QJsonDocument::fromJson(presence::handshakeJson(QStringLiteral("123"))).object();
        QCOMPARE(json.value(QStringLiteral("v")).toInt(), 1);
        QCOMPARE(json.value(QStringLiteral("client_id")).toString(), QStringLiteral("123"));
    }

    void activityWhilePlaying()
    {
        PresencePayload payload;
        payload.title = QStringLiteral("Song \"One\"");
        payload.artist = QStringLiteral("Artist");
        payload.artworkUrl = QStringLiteral("https://example.invalid/a.jpg");
        payload.durationSeconds = 200.0;
        payload.positionSeconds = 50.0;
        const QJsonObject root =
            QJsonDocument::fromJson(presence::activityJson(payload, 42, 1'000'000, QStringLiteral("n1"))).object();
        QCOMPARE(root.value(QStringLiteral("cmd")).toString(), QStringLiteral("SET_ACTIVITY"));
        QCOMPARE(root.value(QStringLiteral("nonce")).toString(), QStringLiteral("n1"));
        const QJsonObject args = root.value(QStringLiteral("args")).toObject();
        QCOMPARE(args.value(QStringLiteral("pid")).toInteger(), 42);
        const QJsonObject activity = args.value(QStringLiteral("activity")).toObject();
        QCOMPARE(activity.value(QStringLiteral("type")).toInt(), 2);
        QCOMPARE(activity.value(QStringLiteral("name")).toString(), QStringLiteral("Last Music"));
        QCOMPARE(activity.value(QStringLiteral("details")).toString(), payload.title);
        QCOMPARE(activity.value(QStringLiteral("state")).toString(), QStringLiteral("Artist"));
        const QJsonObject times = activity.value(QStringLiteral("timestamps")).toObject();
        QCOMPARE(times.value(QStringLiteral("start")).toInteger(), 950'000);
        QCOMPARE(times.value(QStringLiteral("end")).toInteger(), 1'150'000);
        const QJsonObject assets = activity.value(QStringLiteral("assets")).toObject();
        QCOMPARE(assets.value(QStringLiteral("large_image")).toString(), payload.artworkUrl);
        QCOMPARE(assets.value(QStringLiteral("large_text")).toString(), QStringLiteral("Source: Remote"));
    }

    void pausedActivityHasNoTimestamps()
    {
        PresencePayload payload;
        payload.title = QStringLiteral("Song");
        payload.artist = QStringLiteral("Artist");
        payload.durationSeconds = 200.0;
        payload.isPlaying = false;
        payload.isLocal = true;
        payload.artworkUrl = QStringLiteral("http://insecure.invalid/a.jpg");
        const QJsonObject activity = QJsonDocument::fromJson(presence::activityJson(payload, 1, 0, QStringLiteral("n")))
                                         .object()
                                         .value(QStringLiteral("args"))
                                         .toObject()
                                         .value(QStringLiteral("activity"))
                                         .toObject();
        QCOMPARE(activity.value(QStringLiteral("state")).toString(), QStringLiteral("Artist · Paused"));
        QVERIFY(!activity.contains(QStringLiteral("timestamps")));
        // Only https, external assets and bare keys are shown.
        QVERIFY(!activity.contains(QStringLiteral("assets")));
    }

    void externalAssetIsKept()
    {
        PresencePayload payload;
        payload.title = QStringLiteral("Song");
        payload.artworkUrl = QStringLiteral("mp:external/abc/https/x");
        const QJsonObject activity = QJsonDocument::fromJson(presence::activityJson(payload, 1, 0, QStringLiteral("n")))
                                         .object()
                                         .value(QStringLiteral("args"))
                                         .toObject()
                                         .value(QStringLiteral("activity"))
                                         .toObject();
        QCOMPARE(activity.value(QStringLiteral("assets")).toObject().value(QStringLiteral("large_image")).toString(),
                 payload.artworkUrl);
    }

    void artworkForDiscord()
    {
        const QString origin = QStringLiteral("https://api.example.test");
        const QString link = QStringLiteral("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-");
        // An https picture is sent as it is.
        QCOMPARE(presence::presenceArtworkUrl(QStringLiteral("https://img.example.test/a.jpg"), origin),
                 QStringLiteral("https://img.example.test/a.jpg"));
        // An account cover link goes on the account origin, trailing slash or not.
        QCOMPARE(presence::presenceArtworkUrl(link, origin), origin + link);
        QCOMPARE(presence::presenceArtworkUrl(link, origin + QStringLiteral("/")), origin + link);
        // Discord cannot load plain http, so a loopback test origin shows nothing.
        QVERIFY(presence::presenceArtworkUrl(link, QStringLiteral("http://127.0.0.1:8080")).isEmpty());
        QVERIFY(presence::presenceArtworkUrl(link, QString()).isEmpty());
        // Anything else is left out.
        QVERIFY(presence::presenceArtworkUrl(QStringLiteral("/v1/music/art/short"), origin).isEmpty());
        QVERIFY(presence::presenceArtworkUrl(QStringLiteral("http://img.example.test/a.jpg"), origin).isEmpty());
        QVERIFY(presence::presenceArtworkUrl(QStringLiteral("file:///C:/a.jpg"), origin).isEmpty());
        QVERIFY(presence::presenceArtworkUrl(QString(), origin).isEmpty());
    }

    void clearSendsNullActivity()
    {
        const QJsonObject args = QJsonDocument::fromJson(presence::clearJson(7, QStringLiteral("n")))
                                     .object()
                                     .value(QStringLiteral("args"))
                                     .toObject();
        QVERIFY(args.contains(QStringLiteral("activity")));
        QVERIFY(args.value(QStringLiteral("activity")).isNull());
    }

    void recognisesReadyAndError()
    {
        QVERIFY(presence::isReadyFrame(QByteArrayLiteral("{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{}}")));
        QVERIFY(!presence::isReadyFrame(QByteArrayLiteral("{\"cmd\":\"SET_ACTIVITY\",\"evt\":null}")));
        QVERIFY(presence::isErrorFrame(QByteArrayLiteral("{\"cmd\":\"SET_ACTIVITY\",\"evt\":\"ERROR\"}")));
        QVERIFY(!presence::isErrorFrame(QByteArrayLiteral("not json")));
    }

    void fingerprintChangesWithWhatIsShown()
    {
        PresencePayload a;
        a.title = QStringLiteral("Song");
        PresencePayload b = a;
        QCOMPARE(presence::activityFingerprint(a), presence::activityFingerprint(b));
        b.isPlaying = false;
        QVERIFY(presence::activityFingerprint(a) != presence::activityFingerprint(b));
        b = a;
        b.positionSeconds = 1.0;
        QVERIFY(presence::activityFingerprint(a) != presence::activityFingerprint(b));
    }

    void throttlesToOneSendEveryFourSeconds()
    {
        using std::chrono::milliseconds;
        QVERIFY(presence::sendAllowed(std::nullopt));
        QVERIFY(!presence::sendAllowed(milliseconds(0)));
        QVERIFY(!presence::sendAllowed(milliseconds(3999)));
        QVERIFY(presence::sendAllowed(milliseconds(4000)));
    }

    void reconnectBackOffDoublesToThirtySeconds()
    {
        using std::chrono::milliseconds;
        milliseconds delay = presence::ReconnectStart;
        QList<qint64> seen;
        for (int i = 0; i < 7; ++i) {
            seen.append(delay.count());
            delay = presence::nextReconnectDelay(delay);
        }
        QCOMPARE(seen, (QList<qint64>{1000, 2000, 4000, 8000, 16000, 30000, 30000}));
    }
};

QTEST_GUILESS_MAIN(PresenceProtocolTest)
#include "tst_presence_protocol.moc"
