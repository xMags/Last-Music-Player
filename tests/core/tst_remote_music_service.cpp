#include <QFuture>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrl>

#include "ModeServiceFixture.h"
#include "core/catalog/CatalogClient.h"
#include "core/remote/RemoteMusicService.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::mode::SourceMode;
using lmp::test::FakeHttpTransport;

namespace {

template <typename T>
[[nodiscard]] Result<T> resultOf(const QFuture<Result<T>>& future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000)) {
        return lmp::core::Failure::of(FailureKind::Unknown, QStringLiteral("did not finish"));
    }
    return future.result();
}

struct Fixture {
    Fixture()
        : catalog(modes.session.client)
        , remoteMusic({.mode = modes.modes,
                       .session = modes.session.service,
                       .account = modes.session.client,
                       .catalog = catalog,
                       .provider = modes.provider})
    {
    }

    [[nodiscard]] int requests() const { return static_cast<int>(modes.session.transport.requests.size()); }

    lmp::test::ModeServiceFixture modes;
    lmp::core::catalog::CatalogClient catalog;
    lmp::core::remote::RemoteMusicService remoteMusic;
};

lmp::core::Track song()
{
    lmp::core::Track track;
    track.title = QStringLiteral("One");
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("https://music.example.test/1");
    track.durationSeconds = 200;
    return track;
}

} // namespace

// The remote facade: one call, the account service or the provider by mode.
class RemoteMusicServiceTest : public QObject {
    Q_OBJECT

private slots:
    void localModeSendsNothing();
    void accountModeAsksTheAccountService();
    void apiKeyModeAsksTheProviderWithItsKey();
    void answersForAnOlderScopeAreCancelled();
    void rejectedAccountSessionSignsOut();
    void detailPagesFollowTheProvidersCapabilities();
};

void RemoteMusicServiceTest::localModeSendsNothing()
{
    Fixture fixture;
    (void)fixture.modes.modes.start();
    const int before = fixture.requests();
    QCOMPARE(resultOf(fixture.remoteMusic.search(QStringLiteral("a"))).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(resultOf(fixture.remoteMusic.related(song())).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(resultOf(fixture.remoteMusic.resolve(song().sourceUrl)).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(resultOf(fixture.remoteMusic.lyrics({})).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(resultOf(fixture.remoteMusic.importCollection(QStringLiteral("https://music.example.test/l")))
                 .failure()
                 .kind,
             FailureKind::NotAvailable);
    QCOMPARE(fixture.requests(), before);
    QVERIFY(fixture.remoteMusic.scopeKey().isEmpty());
}

void RemoteMusicServiceTest::accountModeAsksTheAccountService()
{
    Fixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("a")));
    QVERIFY(fixture.modes.modes.setMode(SourceMode::Account));
    fixture.modes.session.transport.script(QStringLiteral("/v1/music/search"), FakeHttpTransport::ok("{\"results\":[]}"));
    fixture.modes.session.transport.script(QStringLiteral("/v1/music/lyrics"), FakeHttpTransport::ok("{\"found\":false}"));
    const Result<QByteArray> found = resultOf(fixture.remoteMusic.search(QStringLiteral("a b")));
    QVERIFY(found.isOk());
    const auto& search = fixture.modes.session.transport.requests.last();
    QCOMPARE(search.url.path(), QStringLiteral("/v1/music/search"));
    QCOMPARE(search.header("Authorization"), QByteArray("Bearer bearer-a"));
    QVERIFY(resultOf(fixture.remoteMusic.lyrics(lmp::core::lyrics::lyricsQueryFor(song()))).isOk());
    QCOMPARE(fixture.modes.session.transport.requests.last().url.path(), QStringLiteral("/v1/music/lyrics"));
    QVERIFY(fixture.remoteMusic.scopeKey().startsWith(QStringLiteral("account:")));
}

void RemoteMusicServiceTest::apiKeyModeAsksTheProviderWithItsKey()
{
    Fixture fixture;
    fixture.modes.storeProvider();
    (void)fixture.modes.modes.start();
    QCOMPARE(fixture.modes.modes.mode(), SourceMode::ApiKey);
    fixture.modes.session.transport.script(QStringLiteral("/v1/search"), FakeHttpTransport::ok("{\"results\":[]}"));
    fixture.modes.session.transport.script(QStringLiteral("/v1/related"), FakeHttpTransport::ok("{\"results\":[]}"));
    fixture.modes.session.transport.script(QStringLiteral("/v1/lyrics"), FakeHttpTransport::ok("{\"found\":false}"));
    QVERIFY(resultOf(fixture.remoteMusic.search(QStringLiteral("a"))).isOk());
    const auto& search = fixture.modes.session.transport.requests.last();
    QCOMPARE(search.url.toString(QUrl::FullyEncoded), QStringLiteral("https://provider.example.test/v1/search?q=a"));
    QCOMPARE(search.header("Authorization"), QByteArray("Bearer key-a"));
    QVERIFY(resultOf(fixture.remoteMusic.related(song())).isOk());
    QCOMPARE(fixture.modes.session.transport.requests.last().url.path(), QStringLiteral("/v1/related"));
    lmp::core::Track withReference = song();
    withReference.sourceReference = QStringLiteral("ref-1");
    QVERIFY(resultOf(fixture.remoteMusic.lyrics(lmp::core::lyrics::lyricsQueryFor(withReference))).isOk());
    // The provider takes the link itself, never the account's reference.
    const QString lyricsQuery = fixture.modes.session.transport.requests.last().url.query(QUrl::FullyEncoded);
    QVERIFY(lyricsQuery.contains(QStringLiteral("sourceUrl=https%3A%2F%2Fmusic.example.test%2F1")));
    QVERIFY(!lyricsQuery.contains(QStringLiteral("sourceRef")));
    QVERIFY(fixture.remoteMusic.scopeKey().startsWith(QStringLiteral("provider:")));
}

void RemoteMusicServiceTest::answersForAnOlderScopeAreCancelled()
{
    Fixture fixture;
    fixture.modes.storeProvider();
    (void)fixture.modes.modes.start();
    // The search waits for its answer; the mode changes meanwhile.
    QFuture<Result<QByteArray>> pending = fixture.remoteMusic.search(QStringLiteral("a"));
    QVERIFY(fixture.modes.modes.setMode(SourceMode::LocalOnly));
    QVERIFY(fixture.modes.session.transport.answer(QStringLiteral("/v1/search"), FakeHttpTransport::ok("{\"results\":[]}")));
    const Result<QByteArray> answer = resultOf(pending);
    QVERIFY(!answer.isOk());
    QCOMPARE(answer.failure().kind, FailureKind::Cancelled);
}

void RemoteMusicServiceTest::rejectedAccountSessionSignsOut()
{
    Fixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("a")));
    QVERIFY(fixture.modes.modes.setMode(SourceMode::Account));
    fixture.modes.session.transport.script(QStringLiteral("/v1/music/search"),
                                           FakeHttpTransport::failed(FailureKind::Unauthorized, 401));
    QCOMPARE(resultOf(fixture.remoteMusic.search(QStringLiteral("a"))).failure().kind, FailureKind::Unauthorized);
    QTRY_COMPARE(fixture.modes.session.service.state(), lmp::core::account::AccountSessionState::SignedOut);
}

void RemoteMusicServiceTest::detailPagesFollowTheProvidersCapabilities()
{
    Fixture fixture;
    fixture.modes.storeProvider();
    (void)fixture.modes.modes.start();
    lmp::core::account::LibraryArtistIdentity identity;
    identity.name = QStringLiteral("Some One");
    identity.provider = QStringLiteral("tag-b");
    const int before = fixture.requests();
    // The provider has not listed artist pages: nothing is sent.
    QCOMPARE(resultOf(fixture.remoteMusic.artistDetail(identity)).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(fixture.requests(), before);
}

QTEST_GUILESS_MAIN(RemoteMusicServiceTest)
#include "tst_remote_music_service.moc"
