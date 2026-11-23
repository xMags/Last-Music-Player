#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

#include "AccountSessionFixture.h"
#include "ModeServiceFixture.h"
#include "PlayerFixture.h"
#include "core/account/AccountUrlPolicy.h"
#include "core/catalog/CatalogClient.h"
#include "core/playback/AccountTrackSourceResolver.h"
#include "core/remote/RemoteMusicService.h"
#include "core/playback/PlaybackSourceRouter.h"
#include "core/remote/MediaAddresses.h"
#include "core/settings/SettingKeys.h"
#include "platform/INetworkStatus.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::PlayerController;
using lmp::core::Result;
using lmp::core::Track;
using lmp::test::AccountSessionFixture;
using lmp::test::FakeHttpTransport;
using Notice = lmp::core::PlayerController::PlaybackNotice;
namespace remote = lmp::core::remote;

namespace {

constexpr qint64 kNowMs = 1'700'000'000'000;
const QString kMedia = QStringLiteral("https://media.example.test");

QString streamToken(qint64 offsetMs)
{
    return QStringLiteral("stream.") + QString::number(kNowMs + offsetMs) + QStringLiteral(".signature");
}

QString freshStream(const QString& id = QStringLiteral("a"))
{
    return kMedia + QStringLiteral("/v1/stream/") + id + QStringLiteral("?media_token=") + streamToken(5 * 60 * 1000);
}

class FakeNetworkStatus final : public lmp::platform::INetworkStatus {
public:
    [[nodiscard]] bool online() const override { return true; }
    [[nodiscard]] bool metered() const override { return isMetered; }
    void setChangeHandler(ChangeHandler) override {}

    bool isMetered = false;
};

Track remoteSong(const QString& title = QStringLiteral("Remote"))
{
    Track track;
    track.title = title;
    track.sourceKind = QStringLiteral("remote");
    track.remoteId = title;
    track.sourceUrl = QStringLiteral("https://music.example.test/t/") + title;
    track.durationSeconds = 200.0;
    return track;
}

template <typename T>
[[nodiscard]] Result<T> resultOf(const QFuture<Result<T>>& future)
{
    if (!QTest::qWaitFor([&future]() { return future.isFinished(); }, 3000)) {
        return Failure::of(FailureKind::Unknown, QStringLiteral("did not finish"));
    }
    return future.result();
}

QByteArray resolveAnswer(const QString& stream)
{
    return "{\"result\":{\"streamUrl\":\"" + stream.toUtf8() + "\",\"artworkUrl\":\"\"}}";
}

// The resolver over the remote facade; signIn() validates a session and
// enters Account mode.
struct ResolverFixture {
    ResolverFixture()
        : session(modes.session)
        , catalog(modes.session.client)
        , remoteMusic({.mode = modes.modes,
                       .session = modes.session.service,
                       .account = modes.session.client,
                       .catalog = catalog,
                       .provider = modes.provider})
        , resolver({.remote = remoteMusic,
                    .settings = modes.session.settings,
                    .network = network,
                    .mediaOrigin = kMedia,
                    .now = [] { return kNowMs; }})
    {
        (void)modes.modes.start();
    }

    [[nodiscard]] bool signIn()
    {
        return modes.validate(QStringLiteral("a")) && modes.modes.setMode(lmp::core::mode::SourceMode::Account);
    }

    [[nodiscard]] int resolveCalls() const { return session.transport.requestCount(QStringLiteral("/v1/music/resolve")); }

    lmp::test::ModeServiceFixture modes;
    AccountSessionFixture& session;
    FakeNetworkStatus network;
    lmp::core::catalog::CatalogClient catalog;
    lmp::core::remote::RemoteMusicService remoteMusic;
    lmp::core::AccountTrackSourceResolver resolver;
};

} // namespace

// Account mode playback: stream address rules, the account resolver, the
// router, and what the player does when a remote song fails.
class AccountPlaybackTest : public QObject {
    Q_OBJECT

private slots:
    void streamAddressRules();
    void artworkAddressRules();
    void resolveAnswerParsing();
    void freshSavedStreamIsPlayedAsIs();
    void songIsResolvedThroughTheService();
    void dataSaverAsksForLowQualityOnMeteredConnections();
    void untrustedStreamsAreRefused();
    void resolveNeedsASessionAndAnAddress();
    void rejectedSessionSignsOut();
    void refreshAlwaysAsksTheService();
    void routerSendsSongsByModeAndKind();
    void resolveFailuresAreAnnounced();
    void remoteFailuresInARowStopWithANotice();
    void failedStreamIsFetchedAgainWhereItStopped();
    void streamRecoveryIsLimitedPerWindow();
    void failedRecoveryPausesOnTheSong();
};

void AccountPlaybackTest::streamAddressRules()
{
    QCOMPARE(remote::accountStreamUrl(kMedia + QStringLiteral("/v1/stream/a?x=1#frag"), kMedia),
             kMedia + QStringLiteral("/v1/stream/a?x=1"));
    // Host and path compare without case; the scheme must be written in
    // lower case, as the http(s) check always required.
    QCOMPARE(remote::accountStreamUrl(QStringLiteral("https://MEDIA.example.test/V1/Stream/a"), kMedia),
             QStringLiteral("https://MEDIA.example.test/V1/Stream/a"));
    QVERIFY(remote::accountStreamUrl(QStringLiteral("HTTPS://media.example.test/v1/stream/a"), kMedia).isEmpty());
    QVERIFY(remote::accountStreamUrl(QStringLiteral("https://other.example.test/v1/stream/a"), kMedia).isEmpty());
    QVERIFY(remote::accountStreamUrl(kMedia + QStringLiteral("/v1/streams/a"), kMedia).isEmpty());
    QVERIFY(remote::accountStreamUrl(QStringLiteral("C:/Music/a.mp3"), kMedia).isEmpty());
    QVERIFY(remote::isCurrentProviderPath(kMedia + QStringLiteral("/v1/stream/x"), kMedia + QStringLiteral("//"),
                                          u"/v1/stream/"));

    QCOMPARE(remote::trustedAccountStreamUrl(freshStream(), kMedia, kNowMs), freshStream());
    const QString expiring = kMedia + QStringLiteral("/v1/stream/a?media_token=") + streamToken(10 * 1000);
    QVERIFY(remote::trustedAccountStreamUrl(expiring, kMedia, kNowMs).isEmpty());
    QVERIFY(remote::trustedAccountStreamUrl(freshStream(), QString(), kNowMs).isEmpty());
}

void AccountPlaybackTest::artworkAddressRules()
{
    QCOMPARE(remote::accountArtworkUrl(QStringLiteral("https://images.example.test/a.jpg"), kMedia),
             QStringLiteral("https://images.example.test/a.jpg"));
    QCOMPARE(remote::accountArtworkUrl(QStringLiteral("data:image/png;base64,AA"), kMedia),
             QStringLiteral("data:image/png;base64,AA"));
    QCOMPARE(remote::accountArtworkUrl(kMedia + QStringLiteral("/v1/artwork?url=x#f"), kMedia),
             kMedia + QStringLiteral("/v1/artwork?url=x"));
    QVERIFY(remote::accountArtworkUrl(QStringLiteral("https://other.example.test/v1/artwork?url=x"), kMedia).isEmpty());
}

void AccountPlaybackTest::resolveAnswerParsing()
{
    const remote::ResolvedStream parsed =
        remote::parseResolvedStream("{\"result\":{\"streamUrl\":\"s\",\"artworkUrl\":\"a\"}}");
    QCOMPARE(parsed.streamUrl, QStringLiteral("s"));
    QCOMPARE(parsed.artworkUrl, QStringLiteral("a"));
    QVERIFY(remote::parseResolvedStream("{\"streamUrl\":\"s\"}").streamUrl.isEmpty());
    QVERIFY(remote::parseResolvedStream("{\"result\":{\"streamUrl\":5}}").streamUrl.isEmpty());
    QVERIFY(remote::parseResolvedStream("nope").streamUrl.isEmpty());
}

void AccountPlaybackTest::freshSavedStreamIsPlayedAsIs()
{
    ResolverFixture fixture;
    Track track = remoteSong();
    track.filePath = freshStream();
    const Result<QUrl> resolved = resultOf(fixture.resolver.resolve(track));
    QVERIFY(resolved.isOk());
    QCOMPARE(resolved.value(), QUrl(freshStream()));
    QVERIFY(fixture.session.transport.requests.isEmpty());
}

void AccountPlaybackTest::songIsResolvedThroughTheService()
{
    ResolverFixture fixture;
    QVERIFY(fixture.signIn());
    fixture.session.transport.script(QStringLiteral("/v1/music/resolve"),
                                     FakeHttpTransport::ok(resolveAnswer(freshStream(QStringLiteral("b")))));
    const Result<QUrl> resolved = resultOf(fixture.resolver.resolve(remoteSong()));
    QVERIFY(resolved.isOk());
    QCOMPARE(resolved.value(), QUrl(freshStream(QStringLiteral("b"))));
    const QJsonObject body = QJsonDocument::fromJson(fixture.session.transport.requests.last().body).object();
    QCOMPARE(body.value(QLatin1StringView("url")).toString(), QStringLiteral("https://music.example.test/t/Remote"));
    QVERIFY(!body.contains(QLatin1StringView("quality")));
    QCOMPARE(fixture.session.transport.requests.last().header("Authorization"), QByteArray("Bearer bearer-a"));
}

void AccountPlaybackTest::dataSaverAsksForLowQualityOnMeteredConnections()
{
    ResolverFixture fixture;
    QVERIFY(fixture.signIn());
    fixture.session.settings.setBoolean(lmp::core::settings::keys::DataSaver, true);
    // Data saver alone, on an unmetered connection: normal quality.
    fixture.session.transport.script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok(resolveAnswer(freshStream())));
    QVERIFY(resultOf(fixture.resolver.resolve(remoteSong())).isOk());
    QVERIFY(!QJsonDocument::fromJson(fixture.session.transport.requests.last().body).object().contains(QLatin1StringView("quality")));

    fixture.network.isMetered = true;
    fixture.session.transport.script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok(resolveAnswer(freshStream())));
    QVERIFY(resultOf(fixture.resolver.resolve(remoteSong())).isOk());
    QCOMPARE(QJsonDocument::fromJson(fixture.session.transport.requests.last().body)
                 .object()
                 .value(QLatin1StringView("quality"))
                 .toString(),
             QStringLiteral("low"));
}

void AccountPlaybackTest::untrustedStreamsAreRefused()
{
    ResolverFixture fixture;
    QVERIFY(fixture.signIn());
    for (const QString& stream : {QStringLiteral("https://evil.example.test/v1/stream/a?media_token=") + streamToken(300000),
                                  kMedia + QStringLiteral("/v1/stream/a"), QString()}) {
        fixture.session.transport.script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok(resolveAnswer(stream)));
        QCOMPARE(resultOf(fixture.resolver.resolve(remoteSong())).failure().kind, FailureKind::NotFound);
    }
}

void AccountPlaybackTest::resolveNeedsASessionAndAnAddress()
{
    ResolverFixture fixture;
    QCOMPARE(resultOf(fixture.resolver.resolve(remoteSong())).failure().kind, FailureKind::NotAvailable);
    QVERIFY(fixture.signIn());
    Track noAddress = remoteSong();
    noAddress.sourceUrl.clear();
    QCOMPARE(resultOf(fixture.resolver.resolve(noAddress)).failure().kind, FailureKind::NotFound);
    QCOMPARE(fixture.resolveCalls(), 0);
}

void AccountPlaybackTest::rejectedSessionSignsOut()
{
    ResolverFixture fixture;
    QVERIFY(fixture.signIn());
    fixture.session.transport.script(QStringLiteral("/v1/music/resolve"),
                                     FakeHttpTransport::failed(FailureKind::Unauthorized, 401));
    QCOMPARE(resultOf(fixture.resolver.resolve(remoteSong())).failure().kind, FailureKind::Unauthorized);
    QTRY_COMPARE(fixture.session.service.state(), lmp::core::account::AccountSessionState::SignedOut);
}

void AccountPlaybackTest::refreshAlwaysAsksTheService()
{
    ResolverFixture fixture;
    QVERIFY(fixture.signIn());
    Track track = remoteSong();
    track.filePath = freshStream();
    fixture.session.transport.script(QStringLiteral("/v1/music/resolve"),
                                     FakeHttpTransport::ok(resolveAnswer(freshStream(QStringLiteral("c")))));
    const Result<QUrl> refreshed = resultOf(fixture.resolver.refresh(track));
    QVERIFY(refreshed.isOk());
    QCOMPARE(refreshed.value(), QUrl(freshStream(QStringLiteral("c"))));
    QCOMPARE(fixture.resolveCalls(), 1);
}

void AccountPlaybackTest::routerSendsSongsByModeAndKind()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    FakeSourceResolver local;
    FakeSourceResolver account;
    lmp::core::PlaybackSourceRouter router(local, account, modes.modes);
    QVERIFY(lmp::core::PlaybackSourceRouter::isRemote(remoteSong()));
    Track httpPath;
    httpPath.filePath = QStringLiteral("https://media.example.test/x");
    QVERIFY(lmp::core::PlaybackSourceRouter::isRemote(httpPath));

    Track file;
    file.filePath = QStringLiteral("C:/Music/a.mp3");
    QVERIFY(resultOf(router.resolve(file)).isOk());
    QCOMPARE(local.calls, 1);
    QCOMPARE(resultOf(router.resolve(remoteSong())).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(resultOf(router.refresh(remoteSong())).failure().kind, FailureKind::NotAvailable);
    QCOMPARE(account.calls, 0);

    QVERIFY(modes.validate(QStringLiteral("owner-a")));
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    QVERIFY(resultOf(router.resolve(remoteSong())).isOk());
    QCOMPARE(account.calls, 1);
    (void)resultOf(router.refresh(remoteSong()));
    QCOMPARE(account.refreshCalls, 1);
    QVERIFY(resultOf(router.resolve(file)).isOk());
    QCOMPARE(local.calls, 2);
}

void AccountPlaybackTest::resolveFailuresAreAnnounced()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy notices(player.get(), &PlayerController::playbackNotice);

    fixture.resolver.broken.insert(remoteSong().sourceUrl);
    fixture.resolver.brokenKind = FailureKind::NotAvailable;
    player->playTrack(remoteSong());
    settle();
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).value<Notice>(), Notice::NeedsMusicSource);

    fixture.resolver.brokenKind = FailureKind::NotFound;
    player->playTrack(remoteSong());
    settle();
    QCOMPARE(notices.count(), 2);
    QCOMPARE(notices.last().at(0).value<Notice>(), Notice::NoPlayableSource);
    QCOMPARE(notices.last().at(1).value<Track>().title, QStringLiteral("Remote"));

    // A song on this PC that cannot be found is not announced.
    fixture.resolver.broken.insert(fixture.song(QStringLiteral("a")).filePath);
    player->playTrack(fixture.song(QStringLiteral("a")));
    settle();
    QCOMPARE(notices.count(), 2);
}

void AccountPlaybackTest::remoteFailuresInARowStopWithANotice()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy notices(player.get(), &PlayerController::playbackNotice);
    QList<Track> songs;
    for (const char* name : {"r1", "r2", "r3", "r4"}) {
        songs.append(remoteSong(QString::fromLatin1(name)));
        fixture.resolver.broken.insert(songs.last().sourceUrl);
    }
    player->playInContext(songs.first(), songs);
    settle();
    fixture.clock.advance(250);
    settle();
    fixture.clock.advance(250);
    settle();
    QVERIFY(player->stoppedAfterBrokenTracks());
    QCOMPARE(player->currentTrack()->title, QStringLiteral("r3"));
    QCOMPARE(notices.count(), 4); // three songs, then the stop
    QCOMPARE(notices.last().at(0).value<Notice>(), Notice::RemoteFailuresStopped);
}

void AccountPlaybackTest::failedStreamIsFetchedAgainWhereItStopped()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->playInContext(remoteSong(QStringLiteral("r1")), {remoteSong(QStringLiteral("r1")), remoteSong(QStringLiteral("r2"))});
    settle();
    fixture.audio.start();
    fixture.audio.position(50.0);
    QCOMPARE(fixture.audio.opens.size(), 1);

    fixture.audio.fail();
    settle();
    QCOMPARE(fixture.resolver.refreshCalls, 1);
    QCOMPARE(fixture.audio.opens.size(), 2);
    QCOMPARE(fixture.audio.opens.last().startAtSeconds, 50.0);
    QVERIFY(fixture.audio.opens.last().autoplay);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("r1"));
    fixture.audio.start();
    QCOMPARE(player->state(), PlayerController::State::Playing);

    // Near the end it reopens a second before the end; at the start, from 0.
    fixture.audio.position(199.8);
    fixture.audio.fail();
    settle();
    QCOMPARE(fixture.audio.opens.last().startAtSeconds, 199.0);
    fixture.audio.start();
    fixture.audio.position(0.3);
    fixture.audio.fail();
    settle();
    QCOMPARE(fixture.audio.opens.last().startAtSeconds, 0.0);
}

void AccountPlaybackTest::streamRecoveryIsLimitedPerWindow()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->playTrack(remoteSong());
    settle();
    for (int attempt = 0; attempt < 3; ++attempt) {
        fixture.audio.start();
        fixture.audio.fail();
        settle();
        fixture.clock.advance(1000);
    }
    QCOMPARE(fixture.resolver.refreshCalls, 3);
    fixture.audio.start();
    fixture.audio.fail();
    settle();
    QCOMPARE(fixture.resolver.refreshCalls, 3); // the fourth in 20 s pauses
    QCOMPARE(player->state(), PlayerController::State::Paused);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("Remote"));

    // Play loads the song again; after a quiet spell the count starts over.
    const qsizetype opensBefore = fixture.audio.opens.size();
    player->togglePlay();
    settle();
    QCOMPARE(fixture.audio.opens.size(), opensBefore + 1);
    fixture.clock.advance(21000);
    fixture.audio.start();
    fixture.audio.fail();
    settle();
    QCOMPARE(fixture.resolver.refreshCalls, 4);
}

void AccountPlaybackTest::failedRecoveryPausesOnTheSong()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy notices(player.get(), &PlayerController::playbackNotice);
    player->playInContext(remoteSong(QStringLiteral("r1")), {remoteSong(QStringLiteral("r1")), remoteSong(QStringLiteral("r2"))});
    settle();
    fixture.audio.start();
    fixture.audio.position(30.0);
    fixture.resolver.refreshFailure = FailureKind::NotFound;
    fixture.audio.fail();
    settle();
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.last().at(0).value<Notice>(), Notice::NoPlayableSource);
    QCOMPARE(player->state(), PlayerController::State::Paused);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("r1"));
    fixture.clock.advance(5000);
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("r1")); // not skipped
    QCOMPARE(player->positionSeconds(), 30.0);
}

QTEST_GUILESS_MAIN(AccountPlaybackTest)
#include "tst_account_playback.moc"
