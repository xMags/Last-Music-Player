#include <QCoreApplication>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include "FakePlaybackClock.h"
#include "ModeServiceFixture.h"
#include "core/remote/ArtworkDiskCache.h"
#include "core/remote/ArtworkService.h"

using lmp::core::Failure;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::core::remote::ArtworkService;
using lmp::test::FakeHttpTransport;
using Priority = lmp::core::remote::ArtworkService::Priority;

namespace {

const QString kArtworkPath = QStringLiteral("/v1/music/artwork");

QString picture(int index)
{
    return QStringLiteral("https://images.example.test/%1.jpg").arg(index);
}

void settle()
{
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::processEvents();
    }
}

// An account-mode service over the mode fixture's fake network.
struct ArtworkFixture {
    explicit ArtworkFixture(lmp::core::remote::ArtworkDiskCache* disk = nullptr)
        : service({.client = modes.session.client,
                   .session = modes.session.service,
                   .mode = modes.modes,
                   .http = modes.session.transport,
                   .clock = clock,
                   .disk = disk})
    {
    }

    [[nodiscard]] bool enterAccountMode()
    {
        (void)modes.modes.start();
        return modes.validate(QStringLiteral("owner-a")) && modes.modes.setMode(lmp::core::mode::SourceMode::Account);
    }

    [[nodiscard]] int artworkRequests() const { return modes.session.transport.requestCount(kArtworkPath); }

    bool answer(FakeHttpTransport::Outcome outcome)
    {
        const bool answered = modes.session.transport.answer(kArtworkPath, std::move(outcome));
        settle();
        return answered;
    }

    void advance(std::uint64_t milliseconds)
    {
        clock.advance(milliseconds);
        settle();
    }

    lmp::test::ModeServiceFixture modes;
    FakePlaybackClock clock;
    ArtworkService service;
};

} // namespace

// The artwork scheduler: slots, priorities, sharing, the cache, retries and
// back-off, and dropping everything when the account or mode changes.
class ArtworkServiceTest : public QObject {
    Q_OBJECT

private slots:
    void visibleRequestsUseSixSlots();
    void offScreenWorkTakesTwoSlotsAtMost();
    void promotedRequestStartsAhead();
    void samePictureSharesOneFetchAndIsCached();
    void passingFailuresAreRetriedThenBackedOff();
    void otherFailuresEndTheRequest();
    void theServersRetryAfterIsHonoured();
    void modeChangeDropsEverything();
    void songsWithoutAPictureAreResolvedFirst();
    void outsideAccountModeTheAddressIsFetched();
    void cacheDropsTheLeastRecentlyUsedPastItsBudget();
    void aPictureLargerThanTheBudgetIsNotCached();
    void coverLinksLoadFromTheAccountService();
    void coversWaitForTheStoredSessionCheck();
    void aPictureOnDiskNeedsNoNetwork();
    void keptPicturesSurviveTheMemoryCache();
};

void ArtworkServiceTest::visibleRequestsUseSixSlots()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    QList<QFuture<Result<QByteArray>>> futures;
    for (int index = 0; index < 8; ++index) {
        futures.append(fixture.service.fetch(picture(index)));
    }
    settle();
    QCOMPARE(fixture.artworkRequests(), ArtworkService::MaxActive);
    QCOMPARE(fixture.service.activeCount(), 6);
    QVERIFY(fixture.answer(FakeHttpTransport::ok("img0")));
    QCOMPARE(fixture.artworkRequests(), 7);
    QVERIFY(futures.first().isFinished());
    QCOMPARE(futures.first().result().value(), QByteArray("img0"));
    // The relay is asked with the session, for the picture's address.
    const lmp::core::net::HttpRequest& request = fixture.modes.session.transport.requests.last();
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer bearer-owner-a"));
}

void ArtworkServiceTest::offScreenWorkTakesTwoSlotsAtMost()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    for (int index = 0; index < 4; ++index) {
        (void)fixture.service.fetch(picture(index), {}, Priority::Backfill);
    }
    settle();
    QCOMPARE(fixture.artworkRequests(), 2);
    for (int index = 10; index < 15; ++index) {
        (void)fixture.service.fetch(picture(index));
    }
    settle();
    // Two off-screen and four visible: every slot is taken, one visible waits.
    QCOMPARE(fixture.artworkRequests(), 6);
    QVERIFY(fixture.answer(FakeHttpTransport::ok("x"))); // an off-screen one ends
    // Its slot goes to the waiting visible request, not to off-screen work.
    QCOMPARE(fixture.artworkRequests(), 7);
    QVERIFY(fixture.modes.session.transport.requests.last().url.toString().contains(QStringLiteral("14.jpg")));
}

void ArtworkServiceTest::promotedRequestStartsAhead()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    for (int index = 0; index < 3; ++index) {
        (void)fixture.service.fetch(picture(index), {}, Priority::Backfill);
    }
    settle();
    QCOMPARE(fixture.artworkRequests(), 2);
    fixture.service.promote(picture(2));
    settle();
    QCOMPARE(fixture.artworkRequests(), 3);
    // A visible ask for a waiting off-screen picture promotes it as well.
    (void)fixture.service.fetch(picture(5), {}, Priority::Backfill);
    settle();
    QCOMPARE(fixture.artworkRequests(), 3);
    (void)fixture.service.fetch(picture(5));
    settle();
    QCOMPARE(fixture.artworkRequests(), 4);
}

void ArtworkServiceTest::samePictureSharesOneFetchAndIsCached()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const QFuture<Result<QByteArray>> first = fixture.service.fetch(picture(1));
    const QFuture<Result<QByteArray>> second = fixture.service.fetch(picture(1), {}, Priority::Backfill);
    settle();
    QCOMPARE(fixture.artworkRequests(), 1);
    QVERIFY(fixture.answer(FakeHttpTransport::ok("bytes")));
    QCOMPARE(first.result().value(), QByteArray("bytes"));
    QCOMPARE(second.result().value(), QByteArray("bytes"));
    QVERIFY(fixture.service.isCached(picture(1)));
    QCOMPARE(fixture.service.cachedBytes(), std::size_t{5});

    const QFuture<Result<QByteArray>> third = fixture.service.fetch(picture(1));
    QVERIFY(third.isFinished());
    QCOMPARE(third.result().value(), QByteArray("bytes"));
    QCOMPARE(fixture.artworkRequests(), 1);
}

void ArtworkServiceTest::passingFailuresAreRetriedThenBackedOff()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(picture(1));
    settle();
    int expected = 1;
    const std::array<std::uint64_t, 4> backoff{2000, 5000, 12000, 30000};
    for (int run = 1; run <= ArtworkService::MaxRuns; ++run) {
        // One run: the first try, then 500 ms and 1.5 s later.
        QVERIFY(fixture.answer(FakeHttpTransport::failed(FailureKind::Server, 503)));
        fixture.advance(499);
        QCOMPARE(fixture.artworkRequests(), expected);
        fixture.advance(1);
        QCOMPARE(fixture.artworkRequests(), ++expected);
        QVERIFY(fixture.answer(FakeHttpTransport::failed(FailureKind::RateLimited, 429)));
        fixture.advance(1500);
        QCOMPARE(fixture.artworkRequests(), ++expected);
        QVERIFY(fixture.answer(FakeHttpTransport::failed(FailureKind::Timeout)));
        if (run == ArtworkService::MaxRuns) {
            break;
        }
        // The run is spent: the request waits out its back-off, holding no slot.
        QCOMPARE(fixture.service.activeCount(), 0);
        QVERIFY(!future.isFinished());
        fixture.advance(backoff.at(static_cast<std::size_t>(std::min(run - 1, 3))) - 1);
        QCOMPARE(fixture.artworkRequests(), expected);
        fixture.advance(1);
        QCOMPARE(fixture.artworkRequests(), ++expected);
    }
    QCOMPARE(fixture.artworkRequests(), ArtworkService::MaxRuns * 3);
    QVERIFY(future.isFinished());
    QCOMPARE(future.result().failure().kind, FailureKind::Timeout);
    QCOMPARE(fixture.service.activeCount(), 0);
}

void ArtworkServiceTest::theServersRetryAfterIsHonoured()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(picture(1));
    settle();
    // 20 s is longer than both quick retries and the first back-off: the
    // slot is given back and nothing is asked before the 20 s are up.
    lmp::core::Failure limited = lmp::core::Failure::of(FailureKind::RateLimited, QStringLiteral("slow down"), 429);
    limited.retryAfterSeconds = 20;
    QVERIFY(fixture.answer(limited));
    QCOMPARE(fixture.service.activeCount(), 0);
    fixture.advance(19999);
    QCOMPARE(fixture.artworkRequests(), 1);
    fixture.advance(1);
    QCOMPARE(fixture.artworkRequests(), 2);
    // A wait past the limit ends the request.
    limited.retryAfterSeconds = 3600;
    QVERIFY(fixture.answer(limited));
    QVERIFY(future.isFinished());
    QCOMPARE(future.result().failure().kind, FailureKind::RateLimited);
    QCOMPARE(fixture.service.activeCount(), 0);
}

void ArtworkServiceTest::otherFailuresEndTheRequest()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(picture(1));
    settle();
    QVERIFY(fixture.answer(FakeHttpTransport::failed(FailureKind::NotFound, 404)));
    QVERIFY(future.isFinished());
    QCOMPARE(future.result().failure().kind, FailureKind::NotFound);
    fixture.advance(60000);
    QCOMPARE(fixture.artworkRequests(), 1);
    QCOMPARE(fixture.service.activeCount(), 0);
    // A song with no address at all is refused without asking anyone.
    QCOMPARE(fixture.service.fetch(QString()).result().failure().kind, FailureKind::NotFound);
}

void ArtworkServiceTest::modeChangeDropsEverything()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    (void)fixture.service.fetch(picture(1));
    settle();
    QVERIFY(fixture.answer(FakeHttpTransport::ok("bytes")));
    QVERIFY(fixture.service.isCached(picture(1)));
    const QFuture<Result<QByteArray>> waiting = fixture.service.fetch(picture(2));
    settle();

    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::LocalOnly));
    settle();
    QVERIFY(!fixture.service.isCached(picture(1)));
    QVERIFY(waiting.isFinished());
    QCOMPARE(waiting.result().failure().kind, FailureKind::Cancelled);
    // The orphaned fetch still gives its slot back when it ends.
    QVERIFY(fixture.answer(FakeHttpTransport::ok("late")));
    QCOMPARE(fixture.service.activeCount(), 0);
    QVERIFY(!fixture.service.isCached(picture(2)));
}

void ArtworkServiceTest::songsWithoutAPictureAreResolvedFirst()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    FakeHttpTransport& transport = fixture.modes.session.transport;
    transport.script(QStringLiteral("/v1/music/resolve"),
                     FakeHttpTransport::ok("{\"result\":{\"artworkUrl\":\"https://images.example.test/outer.jpg\","
                                           "\"track\":{\"imageUrl\":\"https://images.example.test/song.jpg\"}}}"));
    const QFuture<Result<QByteArray>> future =
        fixture.service.fetch(QString(), QStringLiteral("https://music.example.test/t/1"));
    settle();
    QCOMPARE(transport.requestCount(QStringLiteral("/v1/music/resolve")), 1);
    QCOMPARE(fixture.artworkRequests(), 1);
    QVERIFY(transport.requests.last().url.toString().contains(QStringLiteral("song.jpg")));
    QVERIFY(fixture.answer(FakeHttpTransport::ok("cover")));
    QCOMPARE(future.result().value(), QByteArray("cover"));
    QVERIFY(fixture.service.isCached(QString(), QStringLiteral("https://music.example.test/t/1")));

    // An answer without a picture ends the request.
    transport.script(QStringLiteral("/v1/music/resolve"), FakeHttpTransport::ok("{\"result\":{}}"));
    const QFuture<Result<QByteArray>> none =
        fixture.service.fetch(QString(), QStringLiteral("https://music.example.test/t/2"));
    settle();
    QVERIFY(none.isFinished());
    QCOMPARE(none.result().failure().kind, FailureKind::NotFound);
}

void ArtworkServiceTest::outsideAccountModeTheAddressIsFetched()
{
    ArtworkFixture fixture;
    (void)fixture.modes.modes.start();
    FakeHttpTransport& transport = fixture.modes.session.transport;
    transport.script(QStringLiteral("/7.jpg"), FakeHttpTransport::ok("direct"));
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(picture(7));
    settle();
    QCOMPARE(future.result().value(), QByteArray("direct"));
    QCOMPARE(fixture.artworkRequests(), 0);
    const lmp::core::net::HttpRequest& request = transport.requests.last();
    QCOMPARE(request.url, QUrl(picture(7)));
    QVERIFY(request.header("Authorization").isEmpty());
    QCOMPARE(request.maxResponseBytes, ArtworkService::MaxPictureBytes);
    // Resolving a song's picture needs Account mode.
    QCOMPARE(fixture.service.fetch(QString(), QStringLiteral("https://music.example.test/t/1")).result().failure().kind,
             FailureKind::NotFound);
}

void ArtworkServiceTest::cacheDropsTheLeastRecentlyUsedPastItsBudget()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    // Three of these do not fit the budget; two do.
    const QByteArray third(static_cast<qsizetype>(ArtworkService::MaxCachedBytes / 3 + 1), 'x');
    for (const int n : {1, 2}) {
        (void)fixture.service.fetch(picture(n));
        settle();
        QVERIFY(fixture.answer(FakeHttpTransport::ok(third)));
    }
    // Using 1 again makes 2 the least recently used.
    QVERIFY(fixture.service.fetch(picture(1)).isFinished());
    (void)fixture.service.fetch(picture(3));
    settle();
    QVERIFY(fixture.answer(FakeHttpTransport::ok(third)));
    QVERIFY(fixture.service.isCached(picture(1)));
    QVERIFY(!fixture.service.isCached(picture(2)));
    QVERIFY(fixture.service.isCached(picture(3)));
    QCOMPARE(fixture.service.cachedBytes(), static_cast<std::size_t>(2 * third.size()));
}

void ArtworkServiceTest::aPictureLargerThanTheBudgetIsNotCached()
{
    ArtworkFixture fixture;
    QVERIFY(fixture.enterAccountMode());
    (void)fixture.service.fetch(picture(1));
    settle();
    QVERIFY(fixture.answer(FakeHttpTransport::ok(QByteArray(5, 'x'))));
    const QByteArray huge(static_cast<qsizetype>(ArtworkService::MaxCachedBytes + 1), 'x');
    QFuture<Result<QByteArray>> future = fixture.service.fetch(picture(2));
    settle();
    QVERIFY(fixture.answer(FakeHttpTransport::ok(huge)));
    // It still reaches the one who asked.
    QCOMPARE(future.result().value().size(), huge.size());
    QVERIFY(!fixture.service.isCached(picture(2)));
    QVERIFY(fixture.service.isCached(picture(1)));
}

void ArtworkServiceTest::coverLinksLoadFromTheAccountService()
{
    ArtworkFixture fixture;
    const QString link = QStringLiteral("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-");
    (void)fixture.modes.modes.start();
    // Outside Account mode a cover link cannot load.
    QCOMPARE(fixture.service.fetch(link).result().failure().kind, FailureKind::NotFound);
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    settle();
    fixture.modes.session.transport.script(link, FakeHttpTransport::ok("cover"));
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(link);
    settle();
    QCOMPARE(future.result().value(), QByteArray("cover"));
    const lmp::core::net::HttpRequest& request = fixture.modes.session.transport.requests.last();
    QCOMPARE(request.url.path(), link);
    QCOMPARE(request.header("Authorization"), QByteArray("Bearer bearer-owner-a"));
    QVERIFY(fixture.service.isCached(QStringLiteral("https://api.example.test") + link));
}

void ArtworkServiceTest::coversWaitForTheStoredSessionCheck()
{
    const QString link = QStringLiteral("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-");
    const QString other = QStringLiteral("/v1/music/art/ZyXwVuTsRqPoNmLkJiHg_-");
    // A launch in Account mode: the stored session is still being checked.
    ArtworkFixture fixture;
    fixture.modes.session.settings.setString(
        lmp::core::settings::keys::RemoteAccessMode,
        QString(lmp::core::mode::sourceModeName(lmp::core::mode::SourceMode::Account)));
    (void)fixture.modes.modes.start();
    fixture.modes.session.storeSession(QStringLiteral("bearer-owner-a"));
    fixture.modes.session.service.restore();
    QVERIFY(fixture.modes.session.service.isRestoring());

    // A cover asked for now waits instead of failing, and sends nothing yet.
    const QFuture<Result<QByteArray>> future = fixture.service.fetch(link);
    settle();
    QVERIFY(!future.isFinished());
    QCOMPARE(fixture.modes.session.transport.requestCount(link), 0);

    // Once the service confirms the session the cover loads under it.
    fixture.modes.session.transport.script(link, FakeHttpTransport::ok("cover"));
    QVERIFY(fixture.modes.session.transport.answer(lmp::test::kProfilePath,
                                                   lmp::test::AccountSessionFixture::profileAnswer(
                                                       QStringLiteral("owner-a"))));
    QVERIFY(QTest::qWaitFor([&future]() { return future.isFinished(); }, 2000));
    QCOMPARE(future.result().value(), QByteArray("cover"));
    QCOMPARE(fixture.modes.session.transport.requests.last().header("Authorization"),
             QByteArray("Bearer bearer-owner-a"));
    QVERIFY(!fixture.modes.session.service.isRestoring());

    // A check that ends signed out fails the waiting cover instead.
    ArtworkFixture rejected;
    rejected.modes.session.settings.setString(
        lmp::core::settings::keys::RemoteAccessMode,
        QString(lmp::core::mode::sourceModeName(lmp::core::mode::SourceMode::Account)));
    (void)rejected.modes.modes.start();
    rejected.modes.session.storeSession(QStringLiteral("bearer-owner-a"));
    rejected.modes.session.service.restore();
    const QFuture<Result<QByteArray>> refused = rejected.service.fetch(other);
    settle();
    QVERIFY(!refused.isFinished());
    QVERIFY(rejected.modes.session.transport.answer(lmp::test::kProfilePath,
                                                    FakeHttpTransport::failed(FailureKind::Unauthorized, 401)));
    QVERIFY(QTest::qWaitFor([&refused]() { return refused.isFinished(); }, 2000));
    QVERIFY(!refused.result());
    QCOMPARE(rejected.modes.session.transport.requestCount(other), 0);
}

void ArtworkServiceTest::aPictureOnDiskNeedsNoNetwork()
{
    QTemporaryDir folder;
    lmp::core::remote::ArtworkDiskCache disk(folder.path());
    {
        ArtworkFixture first(&disk);
        (void)first.modes.modes.start();
        first.modes.session.transport.script(QStringLiteral("/7.jpg"), FakeHttpTransport::ok("direct"));
        const QFuture<Result<QByteArray>> fetched = first.service.fetch(picture(7));
        QTRY_VERIFY(fetched.isFinished());
        QCOMPARE(fetched.result().value(), QByteArray("direct"));
        // Written to disk once fetched.
        const QString key = first.service.diskKey(picture(7));
        QVERIFY(!key.isEmpty());
        QTRY_VERIFY(disk.read(key).result().has_value());
    }
    // The next run: no network asked.
    ArtworkFixture next(&disk);
    (void)next.modes.modes.start();
    const QFuture<Result<QByteArray>> again = next.service.fetch(picture(7));
    QTRY_VERIFY(again.isFinished());
    QCOMPARE(again.result().value(), QByteArray("direct"));
    QVERIFY(next.modes.session.transport.requests.isEmpty());
}

void ArtworkServiceTest::keptPicturesSurviveTheMemoryCache()
{
    QTemporaryDir folder;
    lmp::core::remote::ArtworkDiskCache disk(folder.path());
    ArtworkFixture fixture(&disk);
    (void)fixture.modes.modes.start();
    fixture.modes.session.transport.script(QStringLiteral("/8.jpg"), FakeHttpTransport::ok("cover"));
    const QFuture<bool> kept = fixture.service.keep(picture(8));
    QTRY_VERIFY(kept.isFinished());
    QVERIFY(kept.result());
    QVERIFY(fixture.service.diskReady());
    QTRY_VERIFY(disk.keepIfCached(fixture.service.diskKey(picture(8))).result());
    // Without an account in Account mode there is nobody to keep it for.
    QVERIFY(fixture.enterAccountMode());
    QVERIFY(fixture.service.diskReady());
}

QTEST_GUILESS_MAIN(ArtworkServiceTest)
#include "tst_artwork_service.moc"
