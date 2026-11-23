#include <QObject>
#include <QString>
#include <QTest>

#include "core/catalog/CollectionSourceKey.h"
#include "ui/catalog/CatalogRoutes.h"
#include "ui/catalog/CatalogPageViewModel.h"

namespace catalog = lmp::core::catalog;
namespace routes = lmp::ui::catalog_routes;
using lmp::ui::Route;

// Which page a card opens, the routes' round trips, and the saved-copy link key.
class CatalogRoutesTest : public QObject {
    Q_OBJECT

private slots:
    void cardsOpenTheirPages();
    void chartRoutesRoundTrip();
    void findingAnAlbumCarriesTheSong();
    void linkKeysIgnoreTrackingAndOrder();
};

void CatalogRoutesTest::cardsOpenTheirPages()
{
    catalog::CatalogItem song;
    song.type = catalog::ResourceType::Song;
    QVERIFY(!routes::routeForItem(song));

    catalog::CatalogItem album;
    album.type = catalog::ResourceType::Album;
    album.catalogId = QStringLiteral("123");
    album.title = QStringLiteral("Album");
    const std::optional<Route> catalogueAlbum = routes::routeForItem(album);
    QCOMPARE(catalogueAlbum->kind(), Route::Kind::CatalogDetail);
    QCOMPARE(catalogueAlbum->param(QStringLiteral("type")), QStringLiteral("album"));
    QCOMPARE(catalogueAlbum->param(QStringLiteral("id")), QStringLiteral("123"));
    QVERIFY(lmp::ui::CatalogPageViewModel::handles(*catalogueAlbum));

    album.collectionTarget = lmp::core::account::LibraryArtistIdentity{QStringLiteral("rel-1"), {}, QStringLiteral("Album"),
                                                                       QStringLiteral("tag-b")};
    const std::optional<Route> providerAlbum = routes::routeForItem(album);
    QCOMPARE(providerAlbum->kind(), Route::Kind::SourceAlbum);
    QCOMPARE(routes::identityFromRoute(*providerAlbum).catalogId, QStringLiteral("rel-1"));
    QCOMPARE(routes::identityFromRoute(*providerAlbum).provider, QStringLiteral("tag-b"));

    catalog::CatalogItem playlist;
    playlist.type = catalog::ResourceType::Playlist;
    playlist.catalogId = QStringLiteral("pl.1");
    playlist.externalPlaylistId = QStringLiteral("XPabcdefghij12");
    QCOMPARE(routes::routeForItem(playlist)->kind(), Route::Kind::ExternalPlaylist);
    playlist.externalPlaylistId.clear();
    QCOMPARE(routes::routeForItem(playlist)->param(QStringLiteral("type")), QStringLiteral("playlist"));

    catalog::CatalogItem artist;
    artist.type = catalog::ResourceType::Artist;
    artist.catalogId = QStringLiteral("a1");
    const std::optional<Route> catalogueArtist = routes::routeForItem(artist);
    QCOMPARE(catalogueArtist->param(QStringLiteral("type")), QStringLiteral("artist"));
    // The artist page is its own (the catalogue page shows albums and playlists).
    QVERIFY(!lmp::ui::CatalogPageViewModel::handles(*catalogueArtist));
    artist.artistTarget = catalog::ArtistPageTarget{{QStringLiteral("x"), {}, QStringLiteral("Singer"), QStringLiteral("tag-b")},
                                                    QStringLiteral("99")};
    const std::optional<Route> sourceArtist = routes::routeForItem(artist);
    QCOMPARE(sourceArtist->kind(), Route::Kind::SourceArtist);
    QCOMPARE(sourceArtist->param(QStringLiteral("catalogArtistId")), QStringLiteral("99"));
}

void CatalogRoutesTest::chartRoutesRoundTrip()
{
    const catalog::ChartRef chart{QStringLiteral("in"), catalog::ChartKind::City, QStringLiteral("city-mumbai"),
                                  catalog::ResourceType::Song};
    const Route route = routes::routeForChart(chart);
    QVERIFY(lmp::ui::CatalogPageViewModel::handles(route));
    QCOMPARE(routes::chartFromRoute(route), std::optional<catalog::ChartRef>(chart));
    QVERIFY(!routes::chartFromRoute(Route(Route::Kind::Chart, {{QStringLiteral("region"), QStringLiteral("india")}})));
    QVERIFY(lmp::ui::CatalogPageViewModel::handles(routes::routeForShelf(QStringLiteral("newSongs"))));
}

void CatalogRoutesTest::findingAnAlbumCarriesTheSong()
{
    lmp::core::Track track;
    track.title = QStringLiteral("Song");
    track.album = QStringLiteral("Album");
    track.sourceTag = QStringLiteral("tag-b");
    track.durationSeconds = 181.4;
    const Route route = routes::routeFindingAlbum(track);
    QCOMPARE(route.kind(), Route::Kind::SourceAlbum);
    QCOMPARE(route.param(QStringLiteral("finding")), QStringLiteral("1"));
    QCOMPARE(route.param(QStringLiteral("album")), QStringLiteral("Album"));
    QCOMPARE(route.param(QStringLiteral("durationMs")), QStringLiteral("181400"));
}

void CatalogRoutesTest::linkKeysIgnoreTrackingAndOrder()
{
    QCOMPARE(catalog::canonicalSourceKey(QStringLiteral("https://Music.Example.TEST/album/x/?si=abc&b=2&a=1#frag")),
             QStringLiteral("music.example.test/album/x?a=1&b=2"));
    QCOMPARE(catalog::canonicalSourceKey(QStringLiteral("http://music.example.test/p?utm_source=x&feature=share")),
             QStringLiteral("music.example.test/p"));
    QVERIFY(catalog::canonicalSourceKey(QStringLiteral("ftp://music.example.test/p")).isEmpty());
    QVERIFY(catalog::canonicalSourceKey(QStringLiteral("not a link")).isEmpty());
}

QTEST_MAIN(CatalogRoutesTest)
#include "tst_catalog_routes.moc"
