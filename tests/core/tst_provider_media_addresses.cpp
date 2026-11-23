// Ported from the earlier versions' ProviderHelpersTests (the API key mode
// stream address cases), with the API key artwork builder added.

#include <QObject>
#include <QString>
#include <QTest>

#include "core/remote/MediaAddresses.h"

using lmp::core::remote::providerArtworkUrl;
using lmp::core::remote::providerStreamUrl;
using lmp::core::remote::stableStreamId;

class ProviderMediaAddressesTest : public QObject {
    Q_OBJECT

private slots:
    void existingStreamUrlIsStable();
    void providerStreamUrl_();
    void returnedStreamUrlWins();
    void persistedImportedStreamUrl();
    void directStreamUrl();
    void fallbackTokenAndUnsupportedSources();
    void streamIdIsTheEarlierHash();
    void artworkIsRebasedWithTheKey();
};

void ProviderMediaAddressesTest::existingStreamUrlIsStable()
{
    const QString existing = QStringLiteral(
        "https://provider.example.test/v1/stream/direct%3A1?url=https%3A%2F%2Faudio.example.test%2Fa.mp3&media_token=short");
    const QString stream = providerStreamUrl(existing, {}, {}, QStringLiteral("http://127.0.0.1:4527"), {});
    QVERIFY(!stream.contains(QStringLiteral("media_token=")));
    QVERIFY(stream.contains(QStringLiteral("?url=https%3A%2F%2Faudio.example.test%2Fa.mp3")));

    const QString legacy = QStringLiteral("https://provider.example.test/v1/stream/direct%3A1?access_token=full-key&url="
                                          "https%3A%2F%2Faudio.example.test%2Fa.mp3&media_token=short");
    const QString sanitized = providerStreamUrl(legacy, {}, {}, QStringLiteral("http://127.0.0.1:4527"), {});
    QVERIFY(!sanitized.contains(QStringLiteral("access_token=")));
    QVERIFY(!sanitized.contains(QStringLiteral("media_token=")));
}

void ProviderMediaAddressesTest::providerStreamUrl_()
{
    const QString stream = providerStreamUrl({}, QStringLiteral("https://catalog.example.test/tracks/abc 123"),
                                             QStringLiteral("internal-provider"),
                                             QStringLiteral("https://provider.example.test/"), QStringLiteral("key value"));
    QVERIFY(stream.startsWith(QStringLiteral("https://provider.example.test/v1/stream/remote%3A")));
    QVERIFY(stream.contains(QStringLiteral("?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc%20123")));
    QVERIFY(stream.contains(QStringLiteral("access_token=key%20value")));
}

void ProviderMediaAddressesTest::returnedStreamUrlWins()
{
    const QString stream = providerStreamUrl(
        QStringLiteral("http://old-provider.example.test/v1/stream/service%3Aissued-id?url=https%3A%2F%2Fcatalog.example.test"
                       "%2Ftracks%2Fabc&media_token=short"),
        QStringLiteral("https://catalog.example.test/tracks/abc"), QStringLiteral("service"),
        QStringLiteral("https://provider.example.test/"), QStringLiteral("fresh key"));
    QVERIFY(stream.startsWith(QStringLiteral("https://provider.example.test/v1/stream/service%3Aissued-id")));
    QVERIFY(stream.contains(QStringLiteral("?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc")));
    QVERIFY(!stream.contains(QStringLiteral("media_token=")));
    QVERIFY(stream.contains(QStringLiteral("access_token=fresh%20key")));
}

void ProviderMediaAddressesTest::persistedImportedStreamUrl()
{
    const QString stream = providerStreamUrl(
        QStringLiteral("http://old-provider.example.test/v1/stream/service%3Aissued-id?url=https%3A%2F%2Fcatalog.example.test"
                       "%2Ftracks%2Fabc&media_token=short#lmp=12345"),
        QStringLiteral("https://catalog.example.test/tracks/abc"), QStringLiteral("service"),
        QStringLiteral("https://provider.example.test/"), QStringLiteral("fresh key"));
    QVERIFY(stream.startsWith(QStringLiteral("https://provider.example.test/v1/stream/service%3Aissued-id")));
    QVERIFY(stream.contains(QStringLiteral("?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc")));
    QVERIFY(!stream.contains(QStringLiteral("media_token=")));
    QVERIFY(stream.contains(QStringLiteral("access_token=fresh%20key")));
    QVERIFY(!stream.contains(QStringLiteral("#lmp=")));
}

void ProviderMediaAddressesTest::directStreamUrl()
{
    const QString stream = providerStreamUrl({}, QStringLiteral("https://cdn.example.test/audio/song.mp3"), {},
                                             QStringLiteral("http://127.0.0.1:4527/"), {});
    QVERIFY(stream.startsWith(QStringLiteral("http://127.0.0.1:4527/v1/stream/direct%3A")));
    QVERIFY(stream.contains(QStringLiteral("?url=https%3A%2F%2Fcdn.example.test%2Faudio%2Fsong.mp3")));
    QVERIFY(!stream.contains(QStringLiteral("access_token=")));
}

void ProviderMediaAddressesTest::fallbackTokenAndUnsupportedSources()
{
    const QString fallback =
        providerStreamUrl(QStringLiteral("https://provider.example.test/v1/track/1?access_token=legacy%20key"),
                          QStringLiteral("https://cdn.example.test/audio/song.mp3"), QStringLiteral("direct"),
                          QStringLiteral("http://127.0.0.1:4527"), {});
    QVERIFY(!fallback.contains(QStringLiteral("access_token=")));
    QVERIFY(providerStreamUrl({}, QStringLiteral("custom-scheme://track/1"), QStringLiteral("unsupported"), {}, {}).isEmpty());
    QVERIFY(providerStreamUrl({}, QStringLiteral("file:///C:/Music/a.mp3"), QStringLiteral("direct"), {}, {}).isEmpty());
}

void ProviderMediaAddressesTest::streamIdIsTheEarlierHash()
{
    // The empty text hashes to the offset basis: the earlier versions'
    // 1469598103934665603, one digit short of the usual FNV-1a value, kept so
    // stream ids stay the same.
    QCOMPARE(stableStreamId(QStringLiteral("direct"), QString()), QStringLiteral("direct:14650fb0739d0383"));
    const QString id = stableStreamId(QStringLiteral("remote"), QStringLiteral("https://cdn.example.test/a"));
    QCOMPARE(id.size(), 7 + 16);
    QCOMPARE(id, stableStreamId(QStringLiteral("remote"), QStringLiteral("https://cdn.example.test/a")));
    QVERIFY(id != stableStreamId(QStringLiteral("remote"), QStringLiteral("https://cdn.example.test/b")));
}

void ProviderMediaAddressesTest::artworkIsRebasedWithTheKey()
{
    const QString external = QStringLiteral("https://images.example.test/cover.jpg");
    QCOMPARE(providerArtworkUrl(external, QStringLiteral("https://provider.example.test"), QStringLiteral("k")), external);
    QCOMPARE(providerArtworkUrl(QStringLiteral("cover.jpg"), QStringLiteral("https://provider.example.test"), {}),
             QStringLiteral("cover.jpg"));
    const QString moved =
        providerArtworkUrl(QStringLiteral("http://old.example.test/v1/artwork?url=opaque&access_token=old&media_token=x"),
                           QStringLiteral("https://provider.example.test/"), QStringLiteral("new key"));
    QCOMPARE(moved, QStringLiteral("https://provider.example.test/v1/artwork?url=opaque&access_token=new%20key"));
}

QTEST_GUILESS_MAIN(ProviderMediaAddressesTest)
#include "tst_provider_media_addresses.moc"
