#include <QByteArray>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTest>

#include "FakeHttpTransport.h"
#include "core/account/AccountClient.h"
#include "core/account/AccountMusicContract.h"
#include "core/app/AppIdentity.h"
#include "core/config/PrivateConfig.h"

using lmp::core::AppIdentity;
using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::PrivateConfig;
using lmp::core::Result;
using lmp::core::account::AccountClient;
using lmp::core::net::HttpMethod;
using lmp::core::net::HttpRequest;
using lmp::test::FakeHttpTransport;
namespace account = lmp::core::account;

namespace {

constexpr auto Origin = "https://api.account.example.test";
const QString Session = QStringLiteral("session-a");

PrivateConfig exampleConfig()
{
    PrivateConfig config;
    config.accountApiOrigin = QString::fromLatin1(Origin);
    return config.withDefaults();
}

template <typename T>
[[nodiscard]] Result<T> resultOf(const QFuture<Result<T>>& future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000)) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("future did not finish"));
    }
    return future.result();
}

account::PortableAccountTrack song()
{
    account::PortableAccountTrack track;
    track.id = QStringLiteral("track a/1");
    track.title = QStringLiteral("Track A");
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/a");
    return track;
}

[[nodiscard]] QJsonObject bodyOf(const HttpRequest& request)
{
    return QJsonDocument::fromJson(request.body).object();
}

} // namespace

// The account client's music calls: path, method, headers and body of each,
// and how each answer is read.
class AccountMusicCallsTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void libraryAsksForThePlayHistory();
    void libraryThatIsNotALibrary();
    void likeAndUnlike();
    void invalidLikesSendNothing();
    void historyBatchReturnsAcknowledgedIds();
    void preferencesGetAndPut();
    void playlistCalls();
    void playlistCreateNeedsAnId();
    void songCalls();
    void artworkIsLimited();
    void streamProbeReadsHeadersOnly();
    void failuresPassThrough();
    void clearHistoryDeletesTheHistory();

private:
    // The one request sent since init(), with the common checks done.
    [[nodiscard]] HttpRequest sent(HttpMethod method, const QByteArray& pathAndQuery) const;

    std::unique_ptr<FakeHttpTransport> m_transport;
    std::unique_ptr<AccountClient> m_client;
};

void AccountMusicCallsTest::init()
{
    m_transport = std::make_unique<FakeHttpTransport>();
    m_client = std::make_unique<AccountClient>(*m_transport, exampleConfig());
    QVERIFY(m_client->isConfigured());
}

HttpRequest AccountMusicCallsTest::sent(HttpMethod method, const QByteArray& pathAndQuery) const
{
    if (m_transport->requests.size() != 1) {
        QTest::qFail("expected exactly one request", __FILE__, __LINE__);
        return {};
    }
    const HttpRequest request = m_transport->requests.front();
    if (request.method != method || request.url.toEncoded() != QByteArray(Origin) + pathAndQuery
        || request.header("Authorization") != "Bearer " + Session.toUtf8()
        || request.header("X-Requested-With") != AppIdentity::accountRequestedWith().toUtf8()) {
        qWarning("unexpected request %s", request.url.toEncoded().constData());
        QTest::qFail("the request differs from the expected one", __FILE__, __LINE__);
        return {};
    }
    return request;
}

void AccountMusicCallsTest::libraryAsksForThePlayHistory()
{
    m_transport->script(QStringLiteral("/v1/music/library"),
                        FakeHttpTransport::ok(R"({"tracks":[{"id":"t1","title":"T","sourceUrl":"https://m.example.test/t1"}]})"));
    const Result<account::ParsedAccountLibrary> library = resultOf(m_client->fetchLibrary(Session));
    QVERIFY(library.isOk());
    QVERIFY(library.value().valid);
    QCOMPARE(library.value().tracks.size(), 1);
    const HttpRequest request = sent(HttpMethod::Get, "/v1/music/library?historyLimit=300");
    QVERIFY(request.url.isValid());
    QVERIFY(request.body.isEmpty());
}

void AccountMusicCallsTest::libraryThatIsNotALibrary()
{
    m_transport->script(QStringLiteral("/v1/music/library"), FakeHttpTransport::ok("{\"nothing\":1}"));
    const Result<account::ParsedAccountLibrary> library = resultOf(m_client->fetchLibrary(Session));
    QVERIFY(library.isOk());
    QVERIFY(!library.value().valid);
}

void AccountMusicCallsTest::likeAndUnlike()
{
    m_transport->script(QStringLiteral("/v1/music/liked"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->setLiked(Session, account::serializePortableTrack(song()), true, QString())).isOk());
    const HttpRequest like = sent(HttpMethod::Post, "/v1/music/liked");
    QCOMPARE(bodyOf(like).value(QLatin1StringView("track")).toObject().value(QLatin1StringView("id")).toString(),
             QStringLiteral("track a/1"));
    QCOMPARE(like.header("Content-Type"), QByteArray("application/json; charset=utf-8"));

    init();
    m_transport->script(QStringLiteral("/v1/music/liked"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->setLiked(Session, QByteArray(), false, QStringLiteral("track a/1"))).isOk());
    const HttpRequest unlike = sent(HttpMethod::Delete, "/v1/music/liked?id=track%20a%2F1");
    QVERIFY(unlike.body.isEmpty());
}

void AccountMusicCallsTest::invalidLikesSendNothing()
{
    QCOMPARE(resultOf(m_client->setLiked(Session, "{\"id\":\"x\"}", true, QString())).failure().kind,
             FailureKind::Unknown);
    QCOMPARE(resultOf(m_client->setLiked(Session, QByteArray(), false, QString())).failure().kind, FailureKind::Unknown);
    QCOMPARE(resultOf(m_client->addPlaylistTrack(Session, QStringLiteral("p1"), "{}")).failure().kind,
             FailureKind::Unknown);
    QCOMPARE(resultOf(m_client->removePlaylistTrack(Session, QString(), QStringLiteral("t"))).failure().kind,
             FailureKind::Unknown);
    QVERIFY(m_transport->requests.isEmpty());
}

void AccountMusicCallsTest::historyBatchReturnsAcknowledgedIds()
{
    const QByteArray batch = account::serializeHistoryBatch(
        {{QStringLiteral("e1"), song(), QStringLiteral("2026-08-23T10:20:30Z"), 3}});
    m_transport->script(QStringLiteral("/v1/music/history/batch"),
                        FakeHttpTransport::ok(R"({"accepted":["e1","other"],"duplicates":[]})"));
    const Result<QStringList> acknowledged =
        resultOf(m_client->postHistoryBatch(Session, batch, {QStringLiteral("e1"), QStringLiteral("e2")}));
    QVERIFY(acknowledged.isOk());
    QCOMPARE(acknowledged.value(), QStringList{QStringLiteral("e1")});
    const HttpRequest request = sent(HttpMethod::Post, "/v1/music/history/batch");
    QCOMPARE(request.body, batch);
}

void AccountMusicCallsTest::preferencesGetAndPut()
{
    m_transport->script(QStringLiteral("/v1/music/preferences"),
                        FakeHttpTransport::ok(R"({"preferences":{"theme":"Dark","dataSaver":true}})"));
    const Result<account::PortablePreferences> fetched = resultOf(m_client->fetchPreferences(Session));
    QVERIFY(fetched.isOk());
    QCOMPARE(fetched.value().theme, std::optional<QString>(QStringLiteral("dark")));
    QCOMPARE(fetched.value().dataSaver, std::optional<bool>(true));
    QVERIFY(sent(HttpMethod::Get, "/v1/music/preferences").body.isEmpty());

    init();
    account::PortablePreferences preferences;
    preferences.accent = QStringLiteral("#AABBCC");
    const QByteArray body = account::serializePortablePreferences(preferences);
    m_transport->script(QStringLiteral("/v1/music/preferences"),
                        FakeHttpTransport::ok(R"({"preferences":{"accent":"#aabbcc"}})"));
    const Result<account::PortablePreferences> stored = resultOf(m_client->putPreferences(Session, body));
    QVERIFY(stored.isOk());
    QCOMPARE(stored.value().accent, std::optional<QString>(QStringLiteral("#AABBCC")));
    QCOMPARE(sent(HttpMethod::Put, "/v1/music/preferences").body, body);
}

void AccountMusicCallsTest::playlistCalls()
{
    m_transport->script(QStringLiteral("/v1/music/playlists"),
                        FakeHttpTransport::ok(R"({"playlist":{"id":"p 1","name":"Mix","tracks":[]}})", 201));
    const Result<account::AccountPlaylist> created = resultOf(m_client->createPlaylist(Session, "{\"name\":\"Mix\"}"));
    QVERIFY(created.isOk());
    QCOMPARE(created.value().id, QStringLiteral("p 1"));
    QCOMPARE(created.value().name, QStringLiteral("Mix"));
    QCOMPARE(sent(HttpMethod::Post, "/v1/music/playlists").body, QByteArray("{\"name\":\"Mix\"}"));

    init();
    m_transport->script(QStringLiteral("/v1/music/playlists/p 1"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->updatePlaylist(Session, QStringLiteral("p 1"), "{\"name\":\"New\"}")).isOk());
    QCOMPARE(sent(HttpMethod::Patch, "/v1/music/playlists/p%201").body, QByteArray("{\"name\":\"New\"}"));

    init();
    m_transport->script(QStringLiteral("/v1/music/playlists/p 1"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->deletePlaylist(Session, QStringLiteral("p 1"))).isOk());
    QVERIFY(sent(HttpMethod::Delete, "/v1/music/playlists/p%201").body.isEmpty());

    init();
    m_transport->script(QStringLiteral("/v1/music/playlists/p 1/tracks"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->addPlaylistTrack(Session, QStringLiteral("p 1"),
                                                account::serializePortableTrack(song())))
                .isOk());
    QCOMPARE(bodyOf(sent(HttpMethod::Post, "/v1/music/playlists/p%201/tracks"))
                 .value(QLatin1StringView("track"))
                 .toObject()
                 .value(QLatin1StringView("title"))
                 .toString(),
             QStringLiteral("Track A"));

    init();
    m_transport->script(QStringLiteral("/v1/music/playlists/p 1/tracks/track a/1"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->removePlaylistTrack(Session, QStringLiteral("p 1"), QStringLiteral("track a/1"))).isOk());
    QVERIFY(sent(HttpMethod::Delete, "/v1/music/playlists/p%201/tracks/track%20a%2F1").url.isValid());
}

void AccountMusicCallsTest::playlistCreateNeedsAnId()
{
    m_transport->script(QStringLiteral("/v1/music/playlists"), FakeHttpTransport::ok("{\"playlist\":{}}"));
    QCOMPARE(resultOf(m_client->createPlaylist(Session, "{\"name\":\"Mix\"}")).failure().kind,
             FailureKind::InvalidResponse);
}

void AccountMusicCallsTest::songCalls()
{
    const QString address = QStringLiteral("https://music.example.test/t?a=1&b=2");
    m_transport->script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok("{\"results\":[]}"));
    const Result<QByteArray> resolved = resultOf(m_client->resolve(Session, address, true));
    QCOMPARE(resolved.value(), QByteArray("{\"results\":[]}"));
    const QJsonObject resolveBody = bodyOf(sent(HttpMethod::Post, "/v1/music/resolve"));
    QCOMPARE(resolveBody.value(QLatin1StringView("url")).toString(), address);
    QCOMPARE(resolveBody.value(QLatin1StringView("quality")).toString(), QStringLiteral("low"));

    init();
    m_transport->script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->resolve(Session, address)).isOk());
    QVERIFY(!bodyOf(sent(HttpMethod::Post, "/v1/music/resolve")).contains(QLatin1StringView("quality")));

    init();
    m_transport->script(QStringLiteral("/v1/music/related"), FakeHttpTransport::ok("[]"));
    QVERIFY(resultOf(m_client->related(Session, address)).isOk());
    QVERIFY(sent(HttpMethod::Get, "/v1/music/related?sourceUrl=https%3A%2F%2Fmusic.example.test%2Ft%3Fa%3D1%26b%3D2")
                .url.isValid());

    init();
    m_transport->script(QStringLiteral("/v1/music/import"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->importCollection(Session, address)).isOk());
    QCOMPARE(bodyOf(sent(HttpMethod::Post, "/v1/music/import")).value(QLatin1StringView("url")).toString(), address);
}

void AccountMusicCallsTest::artworkIsLimited()
{
    m_transport->script(QStringLiteral("/v1/music/artwork"), FakeHttpTransport::ok("\x89PNG"));
    QCOMPARE(resultOf(m_client->artwork(Session, QStringLiteral("https://images.example.test/a.jpg"))).value(),
             QByteArray("\x89PNG"));
    const HttpRequest request = sent(HttpMethod::Get, "/v1/music/artwork?url=https%3A%2F%2Fimages.example.test%2Fa.jpg");
    QCOMPARE(request.maxResponseBytes, 12LL * 1024 * 1024);
    QVERIFY(!request.headersOnly);
}

void AccountMusicCallsTest::streamProbeReadsHeadersOnly()
{
    m_transport->script(QStringLiteral("/v1/music/stream"), FakeHttpTransport::ok({}, 206));
    QCOMPARE(resultOf(m_client->probeStream(Session, QStringLiteral("https://media.example.test/s"))).value(), 206);
    QVERIFY(sent(HttpMethod::Get, "/v1/music/stream?url=https%3A%2F%2Fmedia.example.test%2Fs").headersOnly);
}

void AccountMusicCallsTest::failuresPassThrough()
{
    m_transport->script(QStringLiteral("/v1/music/library"), FakeHttpTransport::failed(FailureKind::Unauthorized, 401));
    const Result<account::ParsedAccountLibrary> library = resultOf(m_client->fetchLibrary(Session));
    QCOMPARE(library.failure().kind, FailureKind::Unauthorized);
    QCOMPARE(library.failure().httpStatus, 401);
}

void AccountMusicCallsTest::clearHistoryDeletesTheHistory()
{
    m_transport->script(QStringLiteral("/v1/music/history"), FakeHttpTransport::ok("{}"));
    QVERIFY(resultOf(m_client->clearHistory(Session)).isOk());
    const HttpRequest request = sent(HttpMethod::Delete, "/v1/music/history");
    QVERIFY(request.body.isEmpty());
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer session-a"));

    init();
    m_transport->script(QStringLiteral("/v1/music/history"), FakeHttpTransport::failed(FailureKind::Server, 503));
    QCOMPARE(resultOf(m_client->clearHistory(Session)).failure().httpStatus, 503);
}

QTEST_GUILESS_MAIN(AccountMusicCallsTest)
#include "tst_account_music_calls.moc"
