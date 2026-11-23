#include <QByteArray>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QUrl>

#include <chrono>

#include "FakeHttpTransport.h"
#include "core/net/HttpMessage.h"
#include "core/provider/ProviderClient.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::net::RedirectPolicy;
using lmp::core::provider::isUsableProviderBaseUrl;
using lmp::core::provider::normalizeProviderBaseUrl;
using lmp::core::provider::parseProviderCapabilities;
using lmp::core::provider::ProviderCall;
using lmp::core::provider::ProviderClient;
using lmp::core::provider::ProviderConnection;
using lmp::core::provider::ProviderStatus;
using lmp::test::FakeHttpTransport;

namespace {

const QString kStatusPath = QStringLiteral("/v1/providers");

[[nodiscard]] Result<ProviderStatus> waitFor(QFuture<Result<ProviderStatus>> future)
{
    future.waitForFinished();
    return future.result();
}

} // namespace

class ProviderClientTest : public QObject {
    Q_OBJECT

private slots:
    void baseAddressNormalisation_data();
    void baseAddressNormalisation();
    void usableBaseAddresses_data();
    void usableBaseAddresses();
    void capabilityList_data();
    void capabilityList();
    void statusCallSendsTheKeyToTheBaseAddress();
    void statusCallKeepsABasePath();
    void statusFailureCarriesTheStatus();
    void unusableConnectionsSendNothing_data();
    void unusableConnectionsSendNothing();
    void pathOutsideVersionOneIsRefused();
    void jsonBodyIsSentWithItsType();
    void musicCallsAskTheRightPaths();
    void detailCallsNeedTheCapability();
    void lyricsLeavesOutEmptyFields();
};

void ProviderClientTest::baseAddressNormalisation_data()
{
    QTest::addColumn<QString>("saved");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty uses the local default") << QString() << QStringLiteral("http://127.0.0.1:4527");
    QTest::newRow("trailing slashes") << QStringLiteral("https://music.example.test///")
                                      << QStringLiteral("https://music.example.test");
    QTest::newRow("trailing backslash") << QStringLiteral("https://music.example.test\\")
                                        << QStringLiteral("https://music.example.test");
    QTest::newRow("mixed trailing") << QStringLiteral("http://host.example.test:8080/api/\\/")
                                    << QStringLiteral("http://host.example.test:8080/api");
    QTest::newRow("unchanged otherwise") << QStringLiteral("HTTPS://Music.Example.Test")
                                         << QStringLiteral("HTTPS://Music.Example.Test");
}

void ProviderClientTest::baseAddressNormalisation()
{
    QFETCH(QString, saved);
    QFETCH(QString, expected);
    QCOMPARE(normalizeProviderBaseUrl(saved), expected);
}

void ProviderClientTest::usableBaseAddresses_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<bool>("usable");
    QTest::newRow("default") << QStringLiteral("http://127.0.0.1:4527") << true;
    QTest::newRow("https") << QStringLiteral("https://provider.example.test") << true;
    QTest::newRow("http on the network") << QStringLiteral("http://192.168.1.20:4527") << true;
    QTest::newRow("with a path") << QStringLiteral("https://provider.example.test/music") << true;
    QTest::newRow("upper-case scheme") << QStringLiteral("HTTPS://provider.example.test") << true;
    QTest::newRow("empty") << QString() << false;
    QTest::newRow("no scheme") << QStringLiteral("provider.example.test") << false;
    QTest::newRow("other scheme") << QStringLiteral("ftp://provider.example.test") << false;
    QTest::newRow("file") << QStringLiteral("file:///C:/music") << false;
    QTest::newRow("no host") << QStringLiteral("http://") << false;
    QTest::newRow("user info") << QStringLiteral("https://user:secret@provider.example.test") << false;
    QTest::newRow("query") << QStringLiteral("https://provider.example.test?x=1") << false;
    QTest::newRow("fragment") << QStringLiteral("https://provider.example.test#top") << false;
    QTest::newRow("space") << QStringLiteral("https://provider example.test") << false;
}

void ProviderClientTest::usableBaseAddresses()
{
    QFETCH(QString, base);
    QFETCH(bool, usable);
    QCOMPARE(isUsableProviderBaseUrl(base), usable);
}

void ProviderClientTest::capabilityList_data()
{
    QTest::addColumn<QByteArray>("body");
    QTest::addColumn<QStringList>("expected");
    const QString artist = QStringLiteral("artist-detail-v1");
    const QString album = QStringLiteral("album-detail-v1");
    const QString search = QStringLiteral("search-v1");

    QTest::newRow("top level") << QByteArray(R"({"capabilities":["search-v1","artist-detail-v1","album-detail-v1"]})")
                               << QStringList{search, artist, album};
    QTest::newRow("nested") << QByteArray(R"({"providers":[{"name":"music","capabilities":["album-detail-v1"]}]})")
                            << QStringList{album};
    QTest::newRow("nested without the one asked for")
        << QByteArray(R"({"providers":[{"capabilities":["search-v1"]}]})") << QStringList{search};
    QTest::newRow("both, each once")
        << QByteArray(R"({"capabilities":["album-detail-v1"],"providers":[{"capabilities":["album-detail-v1","x"]}]})")
        << QStringList{album, QStringLiteral("x")};
    QTest::newRow("none listed") << QByteArray(R"({"providers":[]})") << QStringList{};
    QTest::newRow("empty body") << QByteArray() << QStringList{};
    QTest::newRow("not json") << QByteArray("ok") << QStringList{};
    QTest::newRow("array root") << QByteArray(R"(["artist-detail-v1"])") << QStringList{};
    QTest::newRow("top level not an array hides nested")
        << QByteArray(R"({"capabilities":"artist-detail-v1","providers":[{"capabilities":["album-detail-v1"]}]})")
        << QStringList{};
    QTest::newRow("providers not an array keeps top level")
        << QByteArray(R"({"capabilities":["artist-detail-v1"],"providers":{"capabilities":["album-detail-v1"]}})")
        << QStringList{artist};
    QTest::newRow("bad entries are skipped")
        << QByteArray(R"({"providers":[1,{"capabilities":"x"},{"capabilities":["album-detail-v1"]}]})")
        << QStringList{album};
    QTest::newRow("non-string names ignored") << QByteArray(R"({"capabilities":[1,null,"artist-detail-v1"]})")
                                              << QStringList{artist};
    QTest::newRow("exact names only") << QByteArray(R"({"capabilities":["Artist-Detail-V1"]})")
                                      << QStringList{QStringLiteral("Artist-Detail-V1")};

    QByteArray tooMany = "{\"capabilities\":[";
    for (int index = 0; index < 129; ++index) {
        tooMany += index == 0 ? "\"artist-detail-v1\"" : ",\"x\"";
    }
    tooMany += "]}";
    QTest::newRow("more than 128 top-level entries") << tooMany << QStringList{};

    QByteArray manyProviders = "{\"providers\":[";
    for (int index = 0; index < 65; ++index) {
        manyProviders += index == 0 ? "{\"capabilities\":[\"album-detail-v1\"]}" : ",{}";
    }
    manyProviders += "]}";
    QTest::newRow("more than 64 providers") << manyProviders << QStringList{};

    QByteArray huge = R"({"capabilities":["artist-detail-v1"],"pad":")";
    huge += QByteArray(1'000'000, 'a');
    huge += "\"}";
    QTest::newRow("body over the limit") << huge << QStringList{};
}

void ProviderClientTest::capabilityList()
{
    QFETCH(QByteArray, body);
    QFETCH(QStringList, expected);
    QCOMPARE(parseProviderCapabilities(body), expected);
}

void ProviderClientTest::statusCallSendsTheKeyToTheBaseAddress()
{
    FakeHttpTransport transport;
    transport.script(kStatusPath, FakeHttpTransport::ok(R"({"capabilities":["artist-detail-v1"]})"));
    ProviderClient client(transport);
    const Result<ProviderStatus> status =
        waitFor(client.fetchStatus({QStringLiteral("https://provider.example.test//"), QStringLiteral("key-a")}));

    QVERIFY(status.isOk());
    QCOMPARE(status.value().capabilities, QStringList{QStringLiteral("artist-detail-v1")});
    QCOMPARE(transport.requests.size(), 1);
    const auto& request = transport.requests.constFirst();
    QCOMPARE(request.url.toString(), QStringLiteral("https://provider.example.test/v1/providers"));
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer key-a"));
    QCOMPARE(request.header("Accept"), QByteArray("application/json"));
    QCOMPARE(request.timeout, std::chrono::milliseconds(15000));
    QCOMPARE(request.redirects, RedirectPolicy::Refuse);
    QVERIFY(request.maxResponseBytes > 0);
    QVERIFY(request.body.isEmpty());
}

void ProviderClientTest::statusCallKeepsABasePath()
{
    FakeHttpTransport transport;
    transport.script(QStringLiteral("/music/v1/providers"), FakeHttpTransport::ok("{}", 204));
    ProviderClient client(transport);
    const Result<ProviderStatus> status =
        waitFor(client.fetchStatus({QStringLiteral("http://192.168.1.20:4527/music/"), QStringLiteral("key-a")}));
    QVERIFY(status.isOk());
    QVERIFY(status.value().capabilities.isEmpty());
    QCOMPARE(transport.requests.constFirst().url.toString(),
             QStringLiteral("http://192.168.1.20:4527/music/v1/providers"));
}

void ProviderClientTest::statusFailureCarriesTheStatus()
{
    FakeHttpTransport transport;
    transport.script(kStatusPath, FakeHttpTransport::failed(FailureKind::Unauthorized, 401));
    ProviderClient client(transport);
    const Result<ProviderStatus> status =
        waitFor(client.fetchStatus({QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")}));
    QVERIFY(!status.isOk());
    QCOMPARE(status.failure().kind, FailureKind::Unauthorized);
    QCOMPARE(status.failure().httpStatus, 401);
    QVERIFY(!status.failure().detail.contains(QStringLiteral("key-a")));
}

void ProviderClientTest::unusableConnectionsSendNothing_data()
{
    QTest::addColumn<QString>("base");
    QTest::addColumn<QString>("key");
    QTest::addColumn<FailureKind>("kind");
    QTest::newRow("empty key") << QStringLiteral("https://provider.example.test") << QString()
                               << FailureKind::NotAvailable;
    QTest::newRow("other scheme") << QStringLiteral("ftp://provider.example.test") << QStringLiteral("key-a")
                                  << FailureKind::NotAvailable;
    QTest::newRow("user info") << QStringLiteral("https://user@provider.example.test") << QStringLiteral("key-a")
                               << FailureKind::NotAvailable;
    QTest::newRow("line break in key") << QStringLiteral("https://provider.example.test")
                                       << QStringLiteral("key\r\nX-Other: 1") << FailureKind::Unknown;
}

void ProviderClientTest::unusableConnectionsSendNothing()
{
    QFETCH(QString, base);
    QFETCH(QString, key);
    QFETCH(FailureKind, kind);
    FakeHttpTransport transport;
    ProviderClient client(transport);
    const Result<ProviderStatus> status = waitFor(client.fetchStatus({base, key}));
    QVERIFY(!status.isOk());
    QCOMPARE(status.failure().kind, kind);
    QVERIFY(key.isEmpty() || !status.failure().detail.contains(key));
    QCOMPARE(transport.requests.size(), 0);
}

void ProviderClientTest::pathOutsideVersionOneIsRefused()
{
    FakeHttpTransport transport;
    ProviderClient client(transport);
    ProviderCall call;
    call.pathAndQuery = QStringLiteral("/admin");
    QFuture<Result<lmp::core::net::HttpResponse>> sent =
        client.send({QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")}, call);
    sent.waitForFinished();
    QVERIFY(!sent.result().isOk());
    QCOMPARE(sent.result().failure().kind, FailureKind::Unknown);
    QCOMPARE(transport.requests.size(), 0);
}

void ProviderClientTest::jsonBodyIsSentWithItsType()
{
    FakeHttpTransport transport;
    transport.script(QStringLiteral("/v1/resolve"), FakeHttpTransport::ok("{}"));
    ProviderClient client(transport);
    ProviderCall call;
    call.method = lmp::core::net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/resolve");
    call.jsonBody = QByteArrayLiteral("{\"url\":\"x\"}");
    QFuture<Result<lmp::core::net::HttpResponse>> sent =
        client.send({QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")}, call);
    sent.waitForFinished();
    QVERIFY(sent.result().isOk());
    const auto& request = transport.requests.constFirst();
    QCOMPARE(request.header("Content-Type"), QByteArray("application/json; charset=utf-8"));
    QCOMPARE(request.body, call.jsonBody);
}

void ProviderClientTest::musicCallsAskTheRightPaths()
{
    FakeHttpTransport transport;
    for (const char* path : {"/v1/search", "/v1/related", "/v1/resolve", "/v1/import-album"}) {
        transport.script(QString::fromLatin1(path), FakeHttpTransport::ok("{\"results\":[]}"));
    }
    ProviderClient client(transport);
    const ProviderConnection connection{QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")};
    auto search = client.search(connection, QStringLiteral("a & b"));
    auto related = client.related(connection, QStringLiteral("https://music.example.test/x?y=1"));
    auto resolve = client.resolve(connection, QStringLiteral("https://music.example.test/x"));
    auto import = client.importAlbum(connection, QStringLiteral("https://music.example.test/list"));
    for (QFuture<Result<QByteArray>>* future : {&search, &related, &resolve, &import}) {
        future->waitForFinished();
        QVERIFY(future->result().isOk());
        QCOMPARE(future->result().value(), QByteArray("{\"results\":[]}"));
    }
    QCOMPARE(transport.requests.size(), 4);
    QCOMPARE(transport.requests.at(0).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://provider.example.test/v1/search?q=a%20%26%20b"));
    QCOMPARE(transport.requests.at(1).url.toString(QUrl::FullyEncoded),
             QStringLiteral("https://provider.example.test/v1/related?sourceUrl=https%3A%2F%2Fmusic.example.test%2Fx%3Fy%3D1"));
    QCOMPARE(transport.requests.at(2).body, QByteArray("{\"url\":\"https://music.example.test/x\"}"));
    QCOMPARE(transport.requests.at(3).url.path(), QStringLiteral("/v1/import-album"));
    QCOMPARE(transport.requests.at(3).body, QByteArray("{\"url\":\"https://music.example.test/list\"}"));
}

void ProviderClientTest::detailCallsNeedTheCapability()
{
    FakeHttpTransport transport;
    transport.script(QStringLiteral("/v1/artists/detail"), FakeHttpTransport::ok("{}"));
    ProviderClient client(transport);
    const ProviderConnection connection{QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")};
    lmp::core::account::LibraryArtistIdentity identity;
    identity.name = QStringLiteral("Some One");
    identity.provider = QStringLiteral("tag-b");
    auto refused = client.artistDetail(connection, {}, identity);
    refused.waitForFinished();
    QCOMPARE(refused.result().failure().kind, FailureKind::NotAvailable);
    auto album = client.albumDetail(connection, {QStringLiteral("artist-detail-v1")}, identity);
    album.waitForFinished();
    QCOMPARE(album.result().failure().kind, FailureKind::NotAvailable);
    QVERIFY(transport.requests.isEmpty());
    auto artist = client.artistDetail(connection, {QStringLiteral("artist-detail-v1")}, identity);
    artist.waitForFinished();
    QVERIFY(artist.result().isOk());
    QCOMPARE(transport.requests.size(), 1);
    QCOMPARE(transport.requests.constFirst().url.query(QUrl::FullyEncoded), QStringLiteral("name=Some%20One"));
    QCOMPARE(transport.requests.constFirst().timeout, std::chrono::milliseconds(5000));
}

void ProviderClientTest::lyricsLeavesOutEmptyFields()
{
    FakeHttpTransport transport;
    transport.script(QStringLiteral("/v1/lyrics"), FakeHttpTransport::ok("{}"));
    transport.script(QStringLiteral("/v1/lyrics"), FakeHttpTransport::ok("{}"));
    ProviderClient client(transport);
    const ProviderConnection connection{QStringLiteral("https://provider.example.test"), QStringLiteral("key-a")};
    auto bare = client.lyrics(connection, {QStringLiteral("A"), QStringLiteral("T"), {}, 0, {}});
    bare.waitForFinished();
    auto full = client.lyrics(connection, {QStringLiteral("A"), QStringLiteral("T"), QStringLiteral("Al"), 201000,
                                           QStringLiteral("https://music.example.test/x")});
    full.waitForFinished();
    QCOMPARE(transport.requests.at(0).url.query(QUrl::FullyEncoded), QStringLiteral("artist=A&title=T"));
    QCOMPARE(transport.requests.at(1).url.query(QUrl::FullyEncoded),
             QStringLiteral("artist=A&title=T&album=Al&durationMs=201000&sourceUrl=https%3A%2F%2Fmusic.example.test%2Fx"));
}

QTEST_GUILESS_MAIN(ProviderClientTest)
#include "tst_provider_client.moc"
