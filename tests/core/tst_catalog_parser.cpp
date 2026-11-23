#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTest>

#include "CatalogFixtures.h"
#include "core/catalog/CatalogParser.h"
#include "core/catalog/CatalogPresentation.h"

namespace catalog = lmp::core::catalog;
using catalog::ChartKind;
using catalog::ChartRef;
using catalog::ResourceType;
using lmp::test::discoveryFixture;
using lmp::test::json;
using lmp::test::jsonValue;
using lmp::test::testTags;

// The catalogue readers: the earlier parser's cases with neutral values,
// plus the fields and rules the reference client added.
class CatalogParserTest : public QObject {
    Q_OBJECT

private slots:
    void discoveryIsRead();
    void songsCarryTheirTrack();
    void sourceLabelsAreNeverBylines();
    void discoveryForAnotherRegionFails();
    void unsafeArtworkIsDropped();
    void globalShelfInfersItsChart();
    void chartRefsAreValidated();
    void chartPagesAreRead();
    void chartAnswersForAnotherChartFail();
    void globalChartFallsBackToKnownDescriptor();
    void regionsAreRead();
    void extraHomeShelvesAreRead();
    void singlesAndEpsAreMarked();
    void presentationRules();
    void badInputYieldsNothing();
    void searchAllIsRead();
};

void CatalogParserTest::discoveryIsRead()
{
    const lmp::core::Result<catalog::Discovery> result =
        catalog::parseDiscovery(discoveryFixture(), QStringLiteral("IN"), {{QStringLiteral("in"), QStringLiteral("India"), {}}},
                                testTags());
    QVERIFY(result.isOk());
    const catalog::Discovery& discovery = result.value();
    QCOMPARE(discovery.region, QStringLiteral("in"));
    QCOMPARE(discovery.regionName, QStringLiteral("India"));
    QVERIFY(discovery.stale);
    // The empty shelf is dropped.
    QCOMPARE(discovery.shelves.size(), 2);
    const catalog::Shelf& songs = discovery.shelves.at(0);
    QCOMPARE(songs.id, QStringLiteral("top-songs"));
    QCOMPARE(songs.type, ResourceType::Song);
    QVERIFY(songs.chart.has_value());
    QCOMPARE(songs.chart->kind, ChartKind::Global);
    // An untitled song shows as "Untitled"; one without an id is dropped.
    QCOMPARE(songs.items.size(), 2);
    QCOMPARE(songs.items.at(1).title, QStringLiteral("Untitled"));
    const catalog::CatalogItem& song = songs.items.at(0);
    QCOMPARE(song.catalogId, QStringLiteral("1811023667"));
    QCOMPARE(song.title, QStringLiteral("Sample Song"));
    QCOMPARE(song.subtitle, QStringLiteral("Sample Artist"));
    QCOMPARE(song.sourceUrl, QStringLiteral("https://catalog.example.test/albums/sample-album/tracks/1811023667"));
    QCOMPARE(song.genreNames, QStringList({QStringLiteral("Pop"), QStringLiteral("Indian Pop")}));
    QCOMPARE(song.releaseDate, QStringLiteral("2026-01-09"));

    const catalog::CatalogItem& playlist = discovery.shelves.at(1).items.at(0);
    QCOMPARE(playlist.title, QStringLiteral("Sample Playlist"));
    QCOMPARE(playlist.subtitle, QStringLiteral("Example Curator"));
    QCOMPARE(playlist.description, QStringLiteral("A sample playlist."));

    QCOMPARE(discovery.charts.size(), 3);
    QCOMPARE(discovery.charts.at(0).ref.kind, ChartKind::Global);
    QCOMPARE(discovery.charts.at(1).ref.kind, ChartKind::Genre);
    QCOMPARE(discovery.charts.at(2).ref.kind, ChartKind::City);
    QVERIFY(discovery.cityChartGroups.has_value());
    // A grouped city chart must name its region.
    QCOMPARE(discovery.cityChartGroups->indian.charts.size(), 1);
    QCOMPARE(discovery.cityChartGroups->international.charts.size(), 1);
    QCOMPARE(discovery.cityChartGroups->international.charts.at(0).ref.region, QStringLiteral("gb"));
    QVERIFY(discovery.cityChartGroups->international.partial);
    QVERIFY(discovery.moodShelf.has_value());
    QCOMPARE(discovery.moodShelf->items.size(), 2);
}

void CatalogParserTest::songsCarryTheirTrack()
{
    const catalog::Discovery discovery =
        catalog::parseDiscovery(discoveryFixture(), QStringLiteral("in"), {}, testTags()).value();
    // The region name falls back to the upper-case code.
    QCOMPARE(discovery.regionName, QStringLiteral("IN"));
    const catalog::CatalogItem& song = discovery.shelves.at(0).items.at(0);
    QVERIFY(song.track.has_value());
    QCOMPARE(song.track->remoteId, QStringLiteral("tag-a:1811023667"));
    QCOMPARE(song.track->sourceTag, QStringLiteral("tag-a"));
    QCOMPARE(song.track->album, QStringLiteral("Sample Album"));
    QCOMPARE(song.track->albumCatalogId, QStringLiteral("1811023660"));
    QCOMPARE(song.track->artistCatalogId, QStringLiteral("1440817000"));
    QCOMPARE(song.track->durationText, QStringLiteral("3:35"));
    // "Pop" folds first.
    QCOMPARE(song.track->genre, QStringLiteral("Pop"));
    // Without a source address a song cannot play, so it has no track.
    QVERIFY(!discovery.shelves.at(0).items.at(1).track.has_value());

    // An id-less catalogue song is named by the catalogue tag and its id.
    const std::optional<catalog::CatalogItem> bare = catalog::parseCatalogItem(
        jsonValue("{'catalogId':'42','resourceType':'song','title':'T','sourceUrl':'https://catalog.example.test/s/42'}"),
        testTags());
    QVERIFY(bare && bare->track);
    QCOMPARE(bare->track->remoteId, QStringLiteral("tag-a:42"));
    QCOMPARE(bare->track->artist, QStringLiteral("Unknown artist"));
}

void CatalogParserTest::sourceLabelsAreNeverBylines()
{
    const catalog::Discovery discovery =
        catalog::parseDiscovery(discoveryFixture(), QStringLiteral("in"), {}, testTags()).value();
    // The whole curator is a label: no byline. A label prefix on a playlist
    // curator is dropped.
    QVERIFY(discovery.moodShelf->items.at(0).subtitle.isEmpty());
    QCOMPARE(discovery.moodShelf->items.at(1).subtitle, QStringLiteral("Fitness"));
    // Songs keep their artist untouched, apart from a whole label.
    const std::optional<catalog::CatalogItem> song = catalog::parseCatalogItem(
        jsonValue("{'catalogId':'1','resourceType':'song','title':'T','artistName':'Label A Band'}"), testTags());
    QCOMPARE(song->subtitle, QStringLiteral("Label A Band"));
    const std::optional<catalog::CatalogItem> labelled = catalog::parseCatalogItem(
        jsonValue("{'catalogId':'1','resourceType':'album','title':'T','artistName':'LABEL-A','curatorName':'Curator'}"),
        testTags());
    QCOMPARE(labelled->subtitle, QStringLiteral("Curator"));
}

void CatalogParserTest::discoveryForAnotherRegionFails()
{
    QVERIFY(!catalog::parseDiscovery(discoveryFixture(), QStringLiteral("us"), {}, testTags()).isOk());
    QVERIFY(!catalog::parseDiscovery(discoveryFixture(), QStringLiteral("india"), {}, testTags()).isOk());
}

void CatalogParserTest::unsafeArtworkIsDropped()
{
    const catalog::Discovery discovery =
        catalog::parseDiscovery(
            json("{'storefront':'in','shelves':[{'id':'top-songs','title':'Top Songs','resourceType':'song','items':["
                 "{'catalogId':'1','resourceType':'song','title':'One','artworkUrl':'javascript:alert(1)'},"
                 "{'catalogId':'2','resourceType':'song','title':'Two','artworkUrl':'/v1/music/art/abcdefghijklmnopqrstuv'}]}],"
                 "'charts':[{'kind':'genre','id':'14','name':'Pop','title':'Pop','resourceType':'song','artworkUrl':'/relative.jpg'}]}"),
            QStringLiteral("in"), {}, testTags())
            .value();
    QVERIFY(discovery.shelves.at(0).items.at(0).artworkUrl.isEmpty());
    // A cover link is kept as it is.
    QCOMPARE(discovery.shelves.at(0).items.at(1).artworkUrl, QStringLiteral("/v1/music/art/abcdefghijklmnopqrstuv"));
    QVERIFY(discovery.charts.at(0).artworkUrl.isEmpty());
}

void CatalogParserTest::globalShelfInfersItsChart()
{
    const catalog::Discovery discovery =
        catalog::parseDiscovery(json("{'storefront':'in','shelves':[{'id':'top-albums','title':'Top Albums','resourceType':'album',"
                                     "'items':[{'catalogId':'a1','resourceType':'album','name':'Album','trackCount':3}]},"
                                     "{'id':'top-songs','title':'Mislabelled','resourceType':'album',"
                                     "'items':[{'catalogId':'a2','resourceType':'album','name':'Other'}]}]}"),
                                QStringLiteral("in"), {}, testTags())
            .value();
    QCOMPARE(discovery.shelves.size(), 2);
    QVERIFY(discovery.shelves.at(0).chart.has_value());
    QCOMPARE(discovery.shelves.at(0).chart->id, QStringLiteral("top-albums"));
    QCOMPARE(discovery.shelves.at(0).items.at(0).trackCount, 3);
    // A shelf whose id names another type's chart gets none.
    QVERIFY(!discovery.shelves.at(1).chart.has_value());
}

void CatalogParserTest::chartRefsAreValidated()
{
    QVERIFY(catalog::isValidChartRef({QStringLiteral("in"), ChartKind::Genre, QStringLiteral("14"), ResourceType::Song}));
    QVERIFY(!catalog::isValidChartRef({QStringLiteral("india"), ChartKind::Genre, QStringLiteral("14"), ResourceType::Song}));
    QVERIFY(!catalog::isValidChartRef(
        {QStringLiteral("in"), ChartKind::Genre, QStringLiteral("14/../etc"), ResourceType::Song}));
    QVERIFY(!catalog::isValidChartRef(
        {QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-artists"), ResourceType::Artist}));
    QVERIFY(!catalog::isValidChartRef(
        {QStringLiteral("in"), ChartKind::City, QStringLiteral("city-mumbai"), ResourceType::Album}));
    QVERIFY(!catalog::isValidChartRef({QStringLiteral("in"), ChartKind::Genre, QStringLiteral("14"), ResourceType::Playlist}));
    QVERIFY(!catalog::isValidChartRef({QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-hits"), ResourceType::Song}));
    QVERIFY(!catalog::isValidChartRef(
        {QStringLiteral("in"), ChartKind::Genre, QString(161, QLatin1Char('a')), ResourceType::Song}));
    // The type is singular on the wire; a plural is not a chart's type.
    QVERIFY(!catalog::parseChartRef(jsonValue("{'storefront':'in','kind':'genre','id':'14','resourceType':'songs'}")));
    QVERIFY(catalog::parseChartRef(jsonValue("{'kind':'genre','id':'14','resourceType':'song'}"),
                                   {.fallbackRegion = QStringLiteral("IN")}));
    QVERIFY(!catalog::parseChartRef(jsonValue("{'kind':'genre','id':'14','resourceType':'song'}"),
                                    {.fallbackRegion = QStringLiteral("in"), .requireExplicitRegion = true}));
}

void CatalogParserTest::chartPagesAreRead()
{
    const ChartRef topSongs{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-songs"), ResourceType::Song};
    const lmp::core::Result<catalog::Chart> chart = catalog::parseChart(
        json("{'storefront':'in','type':'songs','id':'global:top-songs','title':'Top Songs in India','items':["
             "{'catalogId':'1','resourceType':'song','title':'One','artistName':'A'},"
             "{'catalogId':'2','resourceType':'song','title':'Two','artistName':'B'}],'nextOffset':50}"),
        topSongs, std::nullopt, testTags());
    QVERIFY(chart.isOk());
    QCOMPARE(chart.value().shelf.title, QStringLiteral("Top Songs in India"));
    QCOMPARE(chart.value().shelf.type, ResourceType::Song);
    QCOMPARE(chart.value().shelf.items.size(), 2);
    QCOMPARE(chart.value().shelf.nextOffset, std::optional<int>(50));
    QCOMPARE(chart.value().shelf.id, QStringLiteral("global:top-songs"));
    // Without a descriptor a global chart names itself.
    QCOMPARE(chart.value().descriptor.subtitle, QStringLiteral("Your music region"));

    // City and genre answers name their chart "<kind>:<id>".
    const ChartRef genre{QStringLiteral("in"), ChartKind::Genre, QStringLiteral("20"), ResourceType::Song};
    const lmp::core::Result<catalog::Chart> genreChart = catalog::parseChart(
        json("{'storefront':'in','type':'songs','id':'genre:20','title':'Top Alternative Songs',"
             "'descriptor':{'kind':'genre','id':'20','name':'Alternative','title':'Top Alternative Songs','resourceType':'song'},"
             "'items':[{'catalogId':'1','resourceType':'song','title':'One'}]}"),
        genre, std::nullopt, testTags());
    QVERIFY(genreChart.isOk());
    QCOMPARE(genreChart.value().shelf.items.size(), 1);
    QCOMPARE(genreChart.value().descriptor.name, QStringLiteral("Alternative"));

    // A null next offset ends the list, and so do negative or fractional ones.
    const lmp::core::Result<catalog::ItemPage> last = catalog::parseChartPage(
        json("{'storefront':'in','items':[],'nextOffset':null}"), topSongs, testTags());
    QVERIFY(last.isOk());
    QVERIFY(!last.value().nextOffset);
    QVERIFY(!catalog::parseNextOffset(QJsonValue(-1)));
    QVERIFY(!catalog::parseNextOffset(QJsonValue(2.5)));
    QVERIFY(!catalog::parseNextOffset(QJsonValue(QStringLiteral("soon"))));
    QCOMPARE(catalog::parseNextOffset(QJsonValue(0)), std::optional<int>(0));
}

void CatalogParserTest::chartAnswersForAnotherChartFail()
{
    const ChartRef topSongs{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-songs"), ResourceType::Song};
    QVERIFY(!catalog::parseChart(json("{'storefront':'in','type':'albums','id':'global:top-albums','items':[]}"), topSongs,
                                 std::nullopt, testTags())
                 .isOk());
    QVERIFY(!catalog::parseChart(json("{'storefront':'india','type':'songs','id':'global:top-songs','items':[]}"),
                                 topSongs, std::nullopt, testTags())
                 .isOk());
    const ChartRef genre{QStringLiteral("in"), ChartKind::Genre, QStringLiteral("20"), ResourceType::Song};
    QVERIFY(!catalog::parseChart(
                 json("{'storefront':'in','type':'songs','id':'genre:21','descriptor':{'kind':'genre','id':'21','name':'Rock',"
                      "'title':'Top Rock Songs','resourceType':'song'},'items':[]}"),
                 genre, std::nullopt, testTags())
                 .isOk());
    const ChartRef city{QStringLiteral("in"), ChartKind::City, QStringLiteral("city-mumbai"), ResourceType::Song};
    QVERIFY(!catalog::parseChart(json("{'storefront':'in','descriptor':{'storefront':'in','kind':'city','id':'city-pune',"
                                      "'name':'Pune','title':'Top Songs in Pune','resourceType':'song'},'items':[]}"),
                                 city, std::nullopt, testTags())
                 .isOk());
    QVERIFY(!catalog::parseChart(json("{'storefront':'gb','descriptor':{'storefront':'in','kind':'city','id':'city-mumbai',"
                                      "'name':'Mumbai','title':'Top Songs in Mumbai','resourceType':'song'},'items':[]}"),
                                 city, std::nullopt, testTags())
                 .isOk());
    // A city chart needs its descriptor.
    QVERIFY(!catalog::parseChart(json("{'storefront':'in','items':[]}"), city, std::nullopt, testTags()).isOk());
    QVERIFY(!catalog::parseChartPage(
                 json("{'storefront':'in','descriptor':{'kind':'genre','id':'21','name':'Rock','title':'Rock','resourceType':'song'}}"),
                 genre, testTags())
                 .isOk());
}

void CatalogParserTest::globalChartFallsBackToKnownDescriptor()
{
    const ChartRef topSongs{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-songs"), ResourceType::Song};
    const catalog::ChartDescriptor known{topSongs, QStringLiteral("India"), QStringLiteral("Top Songs in India"),
                                         QStringLiteral("Current music region"), {}, {}};
    const catalog::Chart chart =
        catalog::parseChart(json("{'storefront':'in','items':[]}"), topSongs, known, testTags()).value();
    QCOMPARE(chart.descriptor.name, QStringLiteral("India"));
    QCOMPARE(chart.shelf.title, QStringLiteral("Top Songs in India"));
    const ChartRef topAlbums{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-albums"), ResourceType::Album};
    QCOMPARE(catalog::parseChart(json("{'items':[]}"), topAlbums, std::nullopt, testTags()).value().shelf.title,
             QStringLiteral("Top Albums"));
}

void CatalogParserTest::regionsAreRead()
{
    const QList<catalog::Region> regions = catalog::parseRegions(
        json("{'storefronts':[{'code':'IN','name':'India','defaultLanguageTag':'hi-IN'},{'code':'us','name':''},"
             "{'name':'No Code'},{'code':'usa','name':'Too long'}]}"));
    QCOMPARE(regions.size(), 2);
    QCOMPARE(regions.at(0).code, QStringLiteral("in"));
    QCOMPARE(regions.at(0).defaultLanguageTag, QStringLiteral("hi-IN"));
    // A nameless region shows its code in capitals.
    QCOMPARE(regions.at(1).name, QStringLiteral("US"));
    QCOMPARE(catalog::normalizeRegionCode(QStringLiteral(" GB ")), std::optional<QString>(QStringLiteral("gb")));
}

void CatalogParserTest::extraHomeShelvesAreRead()
{
    const catalog::Discovery discovery =
        catalog::parseDiscovery(
            json("{'storefront':'in',"
                 "'newReleasesShelf':{'items':["
                 "{'catalogId':'rel-1','provider':'tag-b','title':'Stream EP','artistName':'Band','releaseType':'EP',"
                 "'sourceUrl':'https://music.example.test/rel-1'},"
                 "{'catalogId':'123','provider':'tag-a','resourceType':'album','title':'Catalogue Album'},"
                 "{'catalogId':'abc','provider':'tag-a','resourceType':'album','title':'Not numeric'},"
                 "{'catalogId':'x','provider':'other','title':'Other source'}]},"
                 "'newReleaseSongsShelf':{'title':'Fresh','items':["
                 "{'provider':'tag-a','resourceType':'song','catalogId':'77','title':'Cat Song',"
                 "'sourceUrl':'https://catalog.example.test/s/77'},"
                 "{'provider':'tag-b','track':{'id':'tag-b:v1','provider':'tag-b','title':'Stream Song','artist':'Singer',"
                 "'sourceUrl':'https://music.example.test/watch/v1'}},"
                 "{'provider':'other','track':{'id':'o:1','provider':'other','title':'Other','sourceUrl':'https://x.example.test/1'}}]},"
                 "'hitsShelf':{'items':["
                 "{'catalogId':'XPabcdefghij12','provider':'tag-b','title':'Hits','subtitle':'label a'},"
                 "{'catalogId':'XPshort','provider':'tag-b','title':'Too short'},"
                 "{'catalogId':'YYabcdefghij12','provider':'tag-b','title':'Wrong prefix'}]},"
                 "'popularArtistsShelf':{'items':["
                 "{'catalogId':'art-1','name':'Singer','provider':'tag-b','artworkUrl':'https://img.example.test/a.jpg'},"
                 "{'name':'No id','provider':'tag-b'}]}}"),
            QStringLiteral("in"), {}, testTags())
            .value();

    QVERIFY(discovery.newReleasesShelf.has_value());
    QCOMPARE(discovery.newReleasesShelf->title, QStringLiteral("New releases"));
    QCOMPARE(discovery.newReleasesShelf->items.size(), 2);
    const catalog::CatalogItem& streamRelease = discovery.newReleasesShelf->items.at(0);
    QCOMPARE(streamRelease.subtitle, QStringLiteral("EP · Band"));
    QVERIFY(streamRelease.collectionTarget.has_value());
    QCOMPARE(streamRelease.collectionTarget->provider, QStringLiteral("tag-b"));
    // The catalogue's own album opens its album page, without a target.
    QCOMPARE(discovery.newReleasesShelf->items.at(1).subtitle, QStringLiteral("Album"));
    QVERIFY(!discovery.newReleasesShelf->items.at(1).collectionTarget.has_value());

    QVERIFY(discovery.newReleaseSongsShelf.has_value());
    QCOMPARE(discovery.newReleaseSongsShelf->title, QStringLiteral("Fresh"));
    QCOMPARE(discovery.newReleaseSongsShelf->items.size(), 2);
    QCOMPARE(discovery.newReleaseSongsShelf->items.at(1).track->title, QStringLiteral("Stream Song"));

    QVERIFY(discovery.hitsShelf.has_value());
    QCOMPARE(discovery.hitsShelf->items.size(), 1);
    QCOMPARE(discovery.hitsShelf->items.at(0).externalPlaylistId, QStringLiteral("XPabcdefghij12"));
    QVERIFY(discovery.hitsShelf->items.at(0).subtitle.isEmpty());

    QVERIFY(discovery.popularArtistsShelf.has_value());
    QCOMPARE(discovery.popularArtistsShelf->items.size(), 1);
    QVERIFY(discovery.popularArtistsShelf->items.at(0).artistTarget.has_value());
    QCOMPARE(discovery.popularArtistsShelf->items.at(0).artistTarget->identity.catalogId, QStringLiteral("art-1"));

    // Without the private values those shelves stay empty.
    const catalog::Discovery inactive =
        catalog::parseDiscovery(json("{'hitsShelf':{'items':[{'catalogId':'XPabcdefghij12','provider':'tag-b','title':'Hits'}]}}"),
                                QStringLiteral("in"), {}, {})
            .value();
    QVERIFY(!inactive.hitsShelf.has_value());
}

void CatalogParserTest::singlesAndEpsAreMarked()
{
    catalog::CatalogItem one;
    one.type = ResourceType::Album;
    one.trackCount = 1;
    one.subtitle = QStringLiteral("Artist");
    catalog::CatalogItem four = one;
    four.trackCount = 4;
    catalog::CatalogItem unknown = one;
    unknown.trackCount = 0;
    const QList<catalog::CatalogItem> marked = catalog::asSinglesAndEps({one, four, unknown});
    QCOMPARE(marked.at(0).releaseKind, catalog::ReleaseKind::Single);
    QCOMPARE(marked.at(0).subtitle, QStringLiteral("Single · Artist"));
    QCOMPARE(marked.at(1).releaseKind, catalog::ReleaseKind::Ep);
    QCOMPARE(marked.at(2).releaseKind, catalog::ReleaseKind::None);
    QCOMPARE(catalog::withReleaseKind(catalog::CatalogItem{}, catalog::ReleaseKind::Ep).subtitle, QStringLiteral("EP"));
}

void CatalogParserTest::presentationRules()
{
    catalog::Discovery discovery =
        catalog::parseDiscovery(discoveryFixture(), QStringLiteral("in"), {}, testTags()).value();
    const QList<catalog::ChartSection> sections = catalog::buildChartSections(discovery);
    QCOMPARE(sections.size(), 2);
    QCOMPARE(sections.at(0).id, QStringLiteral("explore"));
    QCOMPARE(sections.at(0).charts.size(), 2);
    QCOMPARE(sections.at(1).id, QStringLiteral("cities"));
    // The groups replace the flat city list, home region first.
    QCOMPARE(sections.at(1).charts.size(), 2);
    QCOMPARE(sections.at(1).charts.at(0).ref.region, QStringLiteral("in"));
    QCOMPARE(sections.at(1).charts.at(1).ref.region, QStringLiteral("gb"));
    QVERIFY(sections.at(1).partial);
    discovery.cityChartGroups.reset();
    QCOMPARE(catalog::buildChartSections(discovery).at(1).charts.size(), 1);

    QCOMPARE(catalog::chartPreviewCount(25), 18);
    QCOMPARE(catalog::chartPreviewCount(3), 3);
    QCOMPARE(catalog::moodPreviewCount(25), 12);
    QVERIFY(!catalog::chartSectionShowsSeeAll(4));
    QVERIFY(catalog::chartSectionShowsSeeAll(5));

    catalog::Detail album;
    album.item.type = ResourceType::Album;
    album.item.artworkUrl = QStringLiteral("https://img.example.test/album.jpg");
    lmp::core::Track withCover;
    withCover.artworkUrl = QStringLiteral("https://img.example.test/song.jpg");
    album.tracks = {lmp::core::Track{}, withCover};
    catalog::applyAlbumArtworkToTracks(album);
    QCOMPARE(album.tracks.at(0).artworkUrl, album.item.artworkUrl);
    QCOMPARE(album.tracks.at(1).artworkUrl, album.item.artworkUrl);
    catalog::Detail playlist;
    playlist.item.type = ResourceType::Playlist;
    playlist.item.artworkUrl = QStringLiteral("https://img.example.test/playlist.jpg");
    playlist.tracks = {lmp::core::Track{}};
    catalog::applyAlbumArtworkToTracks(playlist);
    QVERIFY(playlist.tracks.at(0).artworkUrl.isEmpty());
}

void CatalogParserTest::badInputYieldsNothing()
{
    const char* const malformed[] = {
        "",
        "not json at all",
        "{",
        "[]",
        "null",
        "{'shelves': 'not an array'}",
        "{'shelves': [null, 42, 'text']}",
        "{'shelves': [{'id': 7, 'title': true, 'items': [{'title': 5}]}]}",
        "{'storefronts': {'code': 'in'}}",
        "{'items': [{'durationMs': 'long'}], 'nextOffset': 'soon'}",
    };
    const ChartRef topSongs{QStringLiteral("in"), ChartKind::Global, QStringLiteral("top-songs"), ResourceType::Song};
    for (const char* text : malformed) {
        const QByteArray payload = json(text);
        const lmp::core::Result<catalog::Discovery> discovery =
            catalog::parseDiscovery(payload, QStringLiteral("in"), {}, testTags());
        QVERIFY(!discovery.isOk() || discovery.value().shelves.isEmpty());
        QVERIFY(catalog::parseRegions(payload).isEmpty());
        const lmp::core::Result<catalog::ItemPage> page = catalog::parseChartPage(payload, topSongs, testTags());
        QVERIFY(!page.isOk() || (page.value().items.isEmpty() && !page.value().nextOffset));
    }
}

void CatalogParserTest::searchAllIsRead()
{
    const catalog::SearchAll found = catalog::parseSearchAll(
        json("{'songs':[{'id':'tag-b:s1','provider':'tag-b','title':'One','artist':'A','sourceUrl':'https://music.example.test/s1'},"
             "{'id':'tag-b:s2','provider':'tag-b','title':'Two','artist':'B','sourceUrl':'https://music.example.test/s2'}],"
             "'top':{'kind':'song','item':{'id':'tag-b:s2','provider':'tag-b','title':'Two','sourceUrl':'https://music.example.test/s2'}},"
             "'artists':[{'catalogId':'a1','name':'A','provider':'tag-b'},{'name':'No id'}],"
             "'albums':[{'provider':'tag-a','resourceType':'album','catalogId':'77','name':'Cat Album','releaseType':'single'},"
             "{'provider':'tag-a','resourceType':'album','catalogId':'x7','name':'Bad id'},"
             "{'provider':'tag-b','catalogId':'r1','name':'Stream Album','releaseType':'album'}],"
             "'playlists':[{'provider':'tag-a','resourceType':'playlist','catalogId':'pl.abc','name':'Editorial'},"
             "{'provider':'tag-b','catalogId':'XPabcdefghij12','title':'External'}]}"),
        testTags());
    QCOMPARE(found.songs.size(), 2);
    QCOMPARE(found.topKind, QStringLiteral("song"));
    QCOMPARE(found.topSongIndex, 1);
    QCOMPARE(found.artists.size(), 1);
    QCOMPARE(found.artists.at(0).subtitle, QStringLiteral("Artist"));
    QCOMPARE(found.albums.size(), 2);
    QCOMPARE(found.albums.at(0).releaseKind, catalog::ReleaseKind::Single);
    QVERIFY(found.albums.at(1).collectionTarget.has_value());
    QCOMPARE(found.playlists.size(), 2);
    QCOMPARE(found.playlists.at(1).externalPlaylistId, QStringLiteral("XPabcdefghij12"));
    // A top song that is not among the songs, or an unusable top card, is none.
    const catalog::SearchAll lost = catalog::parseSearchAll(
        json("{'songs':[],'top':{'kind':'song','item':{'id':'tag-b:zz','provider':'tag-b','title':'Gone','sourceUrl':'https://music.example.test/z'}}}"),
        testTags());
    QVERIFY(lost.topKind.isEmpty());
    const catalog::SearchAll artistTop =
        catalog::parseSearchAll(json("{'top':{'kind':'artist','item':{'catalogId':'a1','name':'A','provider':'tag-b'}}}"), testTags());
    QCOMPARE(artistTop.topKind, QStringLiteral("artist"));
    QVERIFY(artistTop.topItem.has_value());
}

QTEST_GUILESS_MAIN(CatalogParserTest)
#include "tst_catalog_parser.moc"
