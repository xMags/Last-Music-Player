#include <QFuture>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include <optional>

#include "CatalogFixtures.h"
#include "FakePlaybackClock.h"
#include "ModeServiceFixture.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/settings/SettingKeys.h"

namespace catalog = lmp::core::catalog;
using catalog::ChartKind;
using catalog::ChartRef;
using catalog::ResourceType;
using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::test::FakeHttpTransport;
using lmp::test::json;
using lmp::test::testTags;

namespace {

const QString kOwner = QStringLiteral("owner-a");
const QString kDiscoveryPath = QStringLiteral("/v1/music/catalog/discovery");
const QString kChartsPath = QStringLiteral("/v1/music/catalog/charts");
const QString kResolvePath = QStringLiteral("/v1/music/catalog/artists/resolve");

QByteArray discoveryAnswer(const char* title)
{
    return json((std::string("{'storefront':'in','shelves':[{'id':'s','title':'") + title
                 + "','resourceType':'album','items':[{'catalogId':'a1','resourceType':'album','name':'A'}]}],"
                   "'charts':[{'kind':'global','id':'top-songs','name':'India','title':'Top Songs in India',"
                   "'resourceType':'song'}]}")
                    .c_str());
}

struct ServiceFixture {
    ServiceFixture()
        : client(modes.session.client)
        , service({client, modes.session.service, modes.accounts, modes.session.settings, clock, testTags()})
    {
    }

    template <typename T>
    std::optional<T> outcome(QFuture<T> future)
    {
        return lmp::test::ModeServiceFixture::outcomeOf(std::move(future));
    }

    QString lastQuery(const QString& path) const
    {
        for (qsizetype index = transport().requests.size() - 1; index >= 0; --index) {
            const QUrl url = transport().requests.at(index).url;
            if (url.path() == path) {
                return url.query(QUrl::FullyEncoded);
            }
        }
        return QStringLiteral("<none>");
    }

    FakeHttpTransport& transport() { return modes.session.transport; }
    const FakeHttpTransport& transport() const { return modes.session.transport; }

    lmp::test::ModeServiceFixture modes;
    FakePlaybackClock clock;
    catalog::CatalogClient client;
    catalog::CatalogService service;
};

} // namespace

class CatalogServiceTest : public QObject {
    Q_OBJECT

private slots:
    void requestsAreBuilt();
    void needsASession();
    void aFreshCopyIsReused();
    void aStaleCopyShowsWhileItRefreshes();
    void anExpiredCopyIsShownOnlyWhenLoadingFails();
    void forceSkipsTheCopy();
    void requestsShareOneLoad();
    void everyCallerOfASharedLoadHearsBack();
    void aRegionChangeClearsTheCache();
    void discoveryIsKeptForTheNextLaunch();
    void aGlobalChartUsesTheDescriptorDiscoveryHolds();
    void anArtistMissIsRememberedForFiveMinutes();
};

void CatalogServiceTest::requestsAreBuilt()
{
    ServiceFixture fixture;
    const QString bearer = QStringLiteral("bearer-x");
    (void)fixture.client.discovery(bearer, QStringLiteral("IN"));
    QCOMPARE(fixture.lastQuery(kDiscoveryPath), QStringLiteral("storefront=in&includeCityChartGroups=true"));

    (void)fixture.client.chart(bearer, {QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-albums"), ResourceType::Album},
                               10, 25);
    QCOMPARE(fixture.lastQuery(kChartsPath), QStringLiteral("storefront=in&type=albums&limit=25&offset=10"));
    (void)fixture.client.chart(bearer, {QStringLiteral("gb"), ChartKind::City, QStringLiteral("city-london"), ResourceType::Song},
                               0, 50);
    QCOMPARE(fixture.lastQuery(kChartsPath), QStringLiteral("storefront=gb&type=songs&limit=50&cityId=city-london"));
    // The limit is held to 100 and an offset below 1 is left out.
    (void)fixture.client.chart(bearer, {QStringLiteral("in"), ChartKind::Genre, QStringLiteral("14"), ResourceType::Song}, -3,
                               500);
    QCOMPARE(fixture.lastQuery(kChartsPath), QStringLiteral("storefront=in&type=songs&limit=100&genreId=14"));

    (void)fixture.client.detail(bearer, QStringLiteral("in"), ResourceType::Playlist, QStringLiteral("pl.a/b"));
    QCOMPARE(fixture.transport().requests.last().url.path(QUrl::FullyEncoded),
             QStringLiteral("/v1/music/catalog/playlists/pl.a%2Fb"));

    (void)fixture.client.catalogArtistSection(bearer, QStringLiteral("in"), QStringLiteral("42"),
                                              catalog::ArtistSection::Albums, 50, 50);
    QCOMPARE(fixture.transport().requests.last().url.toString(QUrl::FullyEncoded).section(QLatin1Char('/'), 3),
             QStringLiteral("v1/music/catalog/artists/42/view/full-albums?storefront=in&limit=50&offset=50"));

    (void)fixture.client.providerAlbum(bearer, {{}, {}, QStringLiteral("Some & Album"), QStringLiteral("tag-b")},
                                       QStringLiteral("Band"));
    QCOMPARE(fixture.lastQuery(QStringLiteral("/v1/music/catalog/albums/detail")),
             QStringLiteral("name=Some%20%26%20Album&artist=Band"));

    (void)fixture.client.newReleases(bearer, QStringLiteral("in"), catalog::NewReleasesKind::Songs, 20);
    QCOMPARE(fixture.lastQuery(QStringLiteral("/v1/music/catalog/new-releases")),
             QStringLiteral("storefront=in&kind=songs&offset=20&limit=20"));
    (void)fixture.client.searchAll(bearer, QStringLiteral(" a+b "));
    QCOMPARE(fixture.lastQuery(QStringLiteral("/v1/music/search/all")), QStringLiteral("q=a%2Bb"));

    // Invalid input sends nothing.
    const qsizetype sent = fixture.transport().requests.size();
    QCOMPARE(fixture.outcome(fixture.client.discovery(bearer, QStringLiteral("india")))->failure().kind, FailureKind::Unknown);
    (void)fixture.client.chart(bearer, {QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-hits"), ResourceType::Song},
                               0, 10);
    (void)fixture.client.providerArtist(bearer, {});
    (void)fixture.client.externalPlaylist(bearer, QStringLiteral("XPshort"), testTags());
    (void)fixture.client.search(bearer, QStringLiteral("   "));
    QCOMPARE(fixture.transport().requests.size(), sent);
}

void CatalogServiceTest::needsASession()
{
    ServiceFixture fixture;
    const std::optional<Result<catalog::Discovery>> result = fixture.outcome(fixture.service.discovery());
    QVERIFY(result && !result->isOk());
    QCOMPARE(result->failure().kind, FailureKind::Unauthorized);
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 0);
}

void CatalogServiceTest::aFreshCopyIsReused()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("First")));
    std::optional<Result<catalog::Discovery>> first = fixture.outcome(fixture.service.discovery());
    QVERIFY(first && first->isOk());
    QCOMPARE(first->value().shelves.at(0).title, QStringLiteral("First"));
    fixture.clock.advance(60'000);
    const std::optional<Result<catalog::Discovery>> again = fixture.outcome(fixture.service.discovery());
    QCOMPARE(again->value().shelves.at(0).title, QStringLiteral("First"));
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 1);
}

void CatalogServiceTest::aStaleCopyShowsWhileItRefreshes()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("First")));
    QVERIFY(fixture.outcome(fixture.service.discovery())->isOk());
    QSignalSpy refreshed(&fixture.service, &catalog::CatalogService::refreshed);
    fixture.clock.advance(3 * 60'000);
    // Shown at once from the copy, while a fresh one loads.
    const std::optional<Result<catalog::Discovery>> shown = fixture.outcome(fixture.service.discovery());
    QCOMPARE(shown->value().shelves.at(0).title, QStringLiteral("First"));
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 2);
    // A second visit while it loads starts no other refresh.
    (void)fixture.service.discovery();
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 2);
    QVERIFY(fixture.transport().answer(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Second"))));
    QTRY_COMPARE(refreshed.count(), 1);
    QCOMPARE(refreshed.at(0).at(0).toString(), catalog::discoveryKey(QStringLiteral("in")));
    QCOMPARE(fixture.outcome(fixture.service.discovery())->value().shelves.at(0).title, QStringLiteral("Second"));
}

void CatalogServiceTest::anExpiredCopyIsShownOnlyWhenLoadingFails()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    const QString path = QStringLiteral("/v1/music/catalog/storefronts");
    fixture.transport().script(path, FakeHttpTransport::ok(json("{'storefronts':[{'code':'in','name':'India'}]}")));
    QCOMPARE(fixture.outcome(fixture.service.regions())->value().size(), 1);
    fixture.clock.advance(8ULL * 24 * 60 * 60 * 1000);
    fixture.transport().script(path, FakeHttpTransport::failed(FailureKind::Network));
    const std::optional<Result<QList<catalog::Region>>> offline = fixture.outcome(fixture.service.regions());
    QVERIFY(offline && offline->isOk());
    QCOMPARE(offline->value().at(0).name, QStringLiteral("India"));
    QCOMPARE(fixture.transport().requestCount(path), 2);
    fixture.transport().script(path, FakeHttpTransport::ok(json("{'storefronts':[{'code':'gb','name':'UK'}]}")));
    QCOMPARE(fixture.outcome(fixture.service.regions())->value().at(0).code, QStringLiteral("gb"));
}

void CatalogServiceTest::forceSkipsTheCopy()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("First")));
    QVERIFY(fixture.outcome(fixture.service.discovery())->isOk());
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Forced")));
    QCOMPARE(fixture.outcome(fixture.service.discovery(true))->value().shelves.at(0).title, QStringLiteral("Forced"));
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 2);
}

void CatalogServiceTest::everyCallerOfASharedLoadHearsBack()
{
    // A QFuture keeps one continuation: two pages asking at once must both
    // hear the answer, not only the last to ask.
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    int heard = 0;
    QObject context;
    (void)fixture.service.discovery().then(&context, [&heard](const Result<catalog::Discovery>& answer) {
        heard += answer ? 1 : 0;
    });
    (void)fixture.service.discovery().then(&context, [&heard](const Result<catalog::Discovery>& answer) {
        heard += answer ? 1 : 0;
    });
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 1);
    QVERIFY(fixture.transport().answer(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Shared"))));
    QTRY_COMPARE(heard, 2);
}

void CatalogServiceTest::requestsShareOneLoad()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    QFuture<Result<catalog::Discovery>> first = fixture.service.discovery();
    QFuture<Result<catalog::Discovery>> second = fixture.service.discovery();
    QCOMPARE(fixture.transport().requestCount(kDiscoveryPath), 1);
    QVERIFY(fixture.transport().answer(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Shared"))));
    QCOMPARE(fixture.outcome(first)->value().shelves.at(0).title, QStringLiteral("Shared"));
    QCOMPARE(fixture.outcome(second)->value().shelves.at(0).title, QStringLiteral("Shared"));

    // A forced load replaces one under way; the older answer is not kept.
    fixture.clock.advance(3 * 60'000);
    const QString path = QStringLiteral("/v1/music/catalog/storefronts");
    QFuture<Result<QList<catalog::Region>>> older = fixture.service.regions();
    QFuture<Result<QList<catalog::Region>>> newer = fixture.service.regions(true);
    QVERIFY(fixture.transport().answer(path, FakeHttpTransport::ok(json("{'storefronts':[{'code':'aa'}]}"))));
    QVERIFY(fixture.transport().answer(path, FakeHttpTransport::ok(json("{'storefronts':[{'code':'bb'}]}"))));
    QVERIFY(fixture.outcome(older));
    QVERIFY(fixture.outcome(newer));
    QCOMPARE(fixture.outcome(fixture.service.regions())->value().at(0).code, QStringLiteral("bb"));
}

void CatalogServiceTest::aRegionChangeClearsTheCache()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    QSignalSpy cleared(&fixture.service, &catalog::CatalogService::cleared);
    QFuture<Result<catalog::Discovery>> pending = fixture.service.discovery();
    const std::uint64_t generation = fixture.service.generation();
    fixture.modes.session.settings.setString(lmp::core::settings::keys::CatalogStorefront, QStringLiteral("gb"));
    QCOMPARE(cleared.count(), 1);
    QVERIFY(fixture.service.generation() > generation);
    QCOMPARE(fixture.service.region(), QStringLiteral("gb"));
    // The answer for the old region arrives after the change: not kept.
    QVERIFY(fixture.transport().answer(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Old"))));
    QCOMPARE(fixture.outcome(pending)->failure().kind, FailureKind::Cancelled);
    (void)fixture.service.discovery();
    QCOMPARE(fixture.lastQuery(kDiscoveryPath), QStringLiteral("storefront=gb&includeCityChartGroups=true"));
    // Setting the same region again changes nothing.
    fixture.modes.session.settings.setString(lmp::core::settings::keys::CatalogStorefront, QStringLiteral("GB"));
    QCOMPARE(cleared.count(), 1);
}

void CatalogServiceTest::discoveryIsKeptForTheNextLaunch()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    QVERIFY(!fixture.service.storedDiscovery());
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Saved")));
    QVERIFY(fixture.outcome(fixture.service.discovery())->isOk());
    const std::optional<catalog::Discovery> stored = fixture.service.storedDiscovery();
    QVERIFY(stored);
    QCOMPARE(stored->shelves.at(0).title, QStringLiteral("Saved"));
    // An empty page is not kept over a useful one.
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(json("{'storefront':'in','shelves':[]}")));
    QVERIFY(fixture.outcome(fixture.service.discovery(true))->isOk());
    QCOMPARE(fixture.service.storedDiscovery()->shelves.at(0).title, QStringLiteral("Saved"));
}

void CatalogServiceTest::aGlobalChartUsesTheDescriptorDiscoveryHolds()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    fixture.transport().script(kDiscoveryPath, FakeHttpTransport::ok(discoveryAnswer("Home")));
    QVERIFY(fixture.outcome(fixture.service.discovery())->isOk());
    const ChartRef topSongs{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-songs"), ResourceType::Song};
    fixture.transport().script(kChartsPath, FakeHttpTransport::ok(json("{'storefront':'in','items':[]}")));
    const std::optional<Result<catalog::Chart>> chart = fixture.outcome(fixture.service.chart(topSongs));
    QVERIFY(chart && chart->isOk());
    QCOMPARE(chart->value().descriptor.name, QStringLiteral("India"));
    QCOMPARE(fixture.lastQuery(kChartsPath), QStringLiteral("storefront=in&type=songs&limit=100"));
}

void CatalogServiceTest::anArtistMissIsRememberedForFiveMinutes()
{
    ServiceFixture fixture;
    QVERIFY(fixture.modes.validate(kOwner));
    fixture.transport().script(kResolvePath, FakeHttpTransport::ok(json("{}")));
    std::optional<Result<std::optional<catalog::CatalogItem>>> miss =
        fixture.outcome(fixture.service.resolveArtist(QStringLiteral("Nobody")));
    QVERIFY(miss && miss->isOk() && !miss->value());
    QVERIFY(!fixture.outcome(fixture.service.resolveArtist(QStringLiteral("NOBODY")))->value());
    QCOMPARE(fixture.transport().requestCount(kResolvePath), 1);
    fixture.clock.advance(5 * 60'000 + 1);
    fixture.transport().script(kResolvePath,
                               FakeHttpTransport::ok(json("{'artist':{'catalogId':'7','resourceType':'artist','name':'Nobody'}}")));
    const std::optional<Result<std::optional<catalog::CatalogItem>>> found =
        fixture.outcome(fixture.service.resolveArtist(QStringLiteral("Nobody")));
    QVERIFY(found->value());
    QCOMPARE(found->value()->catalogId, QStringLiteral("7"));
    QCOMPARE(fixture.transport().requestCount(kResolvePath), 2);
    // A found artist is cached like any page.
    QVERIFY(fixture.outcome(fixture.service.resolveArtist(QStringLiteral("nobody")))->value());
    QCOMPARE(fixture.transport().requestCount(kResolvePath), 2);
    // A failure is a failure, not a miss.
    fixture.transport().script(kResolvePath, FakeHttpTransport::failed(FailureKind::Server, 500));
    QVERIFY(!fixture.outcome(fixture.service.resolveArtist(QStringLiteral("Else")))->isOk());
}

QTEST_GUILESS_MAIN(CatalogServiceTest)
#include "tst_catalog_service.moc"
