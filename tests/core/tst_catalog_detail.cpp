#include <QByteArray>
#include <QObject>
#include <QString>
#include <QTest>
#include <QUrlQuery>

#include "CatalogFixtures.h"
#include "core/catalog/CatalogDetailParser.h"
#include "core/catalog/MusicGenres.h"

namespace catalog = lmp::core::catalog;
using catalog::ResourceType;
using lmp::core::account::LibraryArtistIdentity;
using lmp::test::json;
using lmp::test::testTags;

// Detail pages, the identity a lookup sends, provider capabilities and the
// genre folding; the earlier detail contract's cases with neutral values.
class CatalogDetailTest : public QObject {
    Q_OBJECT

private slots:
    void lookupSendsExactlyOneIdentity();
    void capabilitiesAreFound();
    void catalogueAlbumAndPlaylistAreRead();
    void catalogueArtistHasSections();
    void providerArtistIsRead();
    void providerArtistSectionIsChecked();
    void providerAlbumIsRead();
    void externalPlaylistIsRead();
    void genresFold();
};

void CatalogDetailTest::lookupSendsExactlyOneIdentity()
{
    LibraryArtistIdentity identity{QStringLiteral("artist-id"), QStringLiteral("https://music.example.test/artists/source"),
                                   QStringLiteral("Artist Name"), QStringLiteral("tag-b")};
    std::optional<QUrlQuery> query = catalog::detailIdentityQuery(identity, QStringLiteral("Someone"));
    QVERIFY(query);
    QCOMPARE(query->query(), QStringLiteral("catalogId=artist-id"));
    identity.catalogId.clear();
    query = catalog::detailIdentityQuery(identity);
    QVERIFY(query && query->hasQueryItem(QStringLiteral("sourceUrl")) && !query->hasQueryItem(QStringLiteral("name")));
    identity.sourceUrl.clear();
    query = catalog::detailIdentityQuery(identity, QStringLiteral(" Band "));
    QVERIFY(query);
    QCOMPARE(query->queryItemValue(QStringLiteral("name")), QStringLiteral("Artist Name"));
    // A name lookup of an album carries the artist.
    QCOMPARE(query->queryItemValue(QStringLiteral("artist")), QStringLiteral("Band"));
    // An unsafe address never falls back to a same-name lookup.
    identity.sourceUrl = QStringLiteral("https://user:secret@music.example.test/artist");
    QVERIFY(!catalog::detailIdentityQuery(identity));
    identity.sourceUrl.clear();
    identity.name = QStringLiteral("Bad\x01Name");
    QVERIFY(!catalog::detailIdentityQuery(identity));
    identity.name = QString(257, QLatin1Char('n'));
    QVERIFY(!catalog::detailIdentityQuery(identity));
}

void CatalogDetailTest::capabilitiesAreFound()
{
    QVERIFY(catalog::providerSupportsCapability(json("{'capabilities':['search-v1','artist-detail-v1']}"),
                                                catalog::ArtistDetailCapability));
    QVERIFY(catalog::providerSupportsCapability(json("{'providers':[{'name':'music','capabilities':['album-detail-v1']}]}"),
                                                catalog::AlbumDetailCapability));
    QVERIFY(!catalog::providerSupportsCapability(json("{'providers':[{'capabilities':['search-v1']}]}"),
                                                 catalog::ArtistDetailCapability));
    QVERIFY(!catalog::providerSupportsCapability("not json", catalog::ArtistDetailCapability));
}

void CatalogDetailTest::catalogueAlbumAndPlaylistAreRead()
{
    const lmp::core::Result<catalog::Detail> album = catalog::parseCatalogDetail(
        json("{'storefront':'in','album':{'catalogId':'1811023660','resourceType':'album','name':'Sample Album',"
             "'artistName':'Sample Artist','trackCount':2,'editorialNotes':'Notes about the album.'},"
             "'tracks':[{'catalogId':'1','resourceType':'song','title':'One','artistName':'Sample Artist',"
             "'sourceUrl':'https://catalog.example.test/s/1'},"
             "{'catalogId':'2','resourceType':'song','title':'Two','artistName':'Sample Artist',"
             "'sourceUrl':'https://catalog.example.test/s/2'},"
             "{'catalogId':'3','resourceType':'song','title':'No address'}]}"),
        ResourceType::Album, testTags());
    QVERIFY(album.isOk());
    QCOMPARE(album.value().item.title, QStringLiteral("Sample Album"));
    QCOMPARE(album.value().item.type, ResourceType::Album);
    QCOMPARE(album.value().item.trackCount, 2);
    QCOMPARE(album.value().item.description, QStringLiteral("Notes about the album."));
    // A song that cannot play is left out of the list.
    QCOMPARE(album.value().tracks.size(), 2);
    QVERIFY(album.value().sections.isEmpty());

    const lmp::core::Result<catalog::Detail> playlist = catalog::parseCatalogDetail(
        json("{'playlist':{'catalogId':'pl.1','resourceType':'playlist','name':'Sample Playlist',"
             "'editorialNotes':{'short':'Short note'}},"
             "'tracks':[{'catalogId':'1','resourceType':'song','title':'One','sourceUrl':'https://catalog.example.test/s/1'}]}"),
        ResourceType::Playlist, testTags());
    QVERIFY(playlist.isOk());
    QCOMPARE(playlist.value().item.title, QStringLiteral("Sample Playlist"));
    QCOMPARE(playlist.value().item.description, QStringLiteral("Short note"));
    QCOMPARE(playlist.value().tracks.size(), 1);

    // Without the item the page is unavailable.
    const lmp::core::Result<catalog::Detail> missing =
        catalog::parseCatalogDetail(json("{'tracks':[]}"), ResourceType::Album, testTags());
    QVERIFY(!missing.isOk());
    QCOMPARE(missing.failure().kind, lmp::core::FailureKind::NotFound);
    QVERIFY(!catalog::parseCatalogDetail("{", ResourceType::Album, testTags()).isOk());
}

void CatalogDetailTest::catalogueArtistHasSections()
{
    const catalog::Detail artist =
        catalog::parseCatalogDetail(
            json("{'artist':{'catalogId':'a1','resourceType':'artist','name':'Singer'},"
                 "'latestRelease':[{'catalogId':'r1','resourceType':'album','name':'Newest'}],"
                 "'topSongs':[{'catalogId':'s1','resourceType':'song','title':'Hit'}],"
                 "'albums':[],"
                 "'singles':[{'catalogId':'r2','resourceType':'album','name':'One','trackCount':1},"
                 "{'catalogId':'r3','resourceType':'album','name':'Four','trackCount':4}],"
                 "'similarArtists':[{'catalogId':'a2','resourceType':'artist','name':'Other'}]}"),
            ResourceType::Artist, testTags())
            .value();
    QCOMPARE(artist.sections.size(), 4);
    QCOMPARE(artist.sections.at(0).id, QStringLiteral("latest-release"));
    QCOMPARE(artist.sections.at(1).type, ResourceType::Song);
    QCOMPARE(artist.sections.at(2).title, QStringLiteral("Singles & EPs"));
    QCOMPARE(artist.sections.at(2).items.at(0).releaseKind, catalog::ReleaseKind::Single);
    QCOMPARE(artist.sections.at(2).items.at(1).releaseKind, catalog::ReleaseKind::Ep);
    QCOMPARE(artist.sections.at(3).type, ResourceType::Artist);
}

void CatalogDetailTest::providerArtistIsRead()
{
    const LibraryArtistIdentity identity{{}, {}, QStringLiteral("Fallback Name"), QStringLiteral("tag-b")};
    const lmp::core::Result<catalog::Detail> result = catalog::parseProviderArtistDetail(
        json("{'artist':{'catalogId':'artist-1','name':'Example Artist','provider':'tag-b',"
             "'sourceUrl':'https://music.example.test/artists/artist-1',"
             "'artworkUrl':'https://img.example.test/artist-1','biography':'<b>Example</b> biography',"
             "'genreNames':['Pop','R&B']},"
             "'topSongs':[{'id':'track-1','catalogId':'song-1','albumCatalogId':'album-1','artistCatalogId':'artist-1',"
             "'title':'First Song','artist':'Example Artist','album':'First Album','provider':'tag-b',"
             "'sourceUrl':'https://music.example.test/watch/track-1','durationMs':183000},"
             "{'title':'No address'}],"
             "'releases':["
             "{'catalogId':'album-1','name':'First Album','artistName':'Example Artist','provider':'tag-b',"
             "'releaseType':'album','releaseDate':'2026-01-02','trackCount':9},"
             "{'catalogId':'single-1','name':'Named - Single','provider':'tag-b','releaseType':'album','trackCount':1},"
             "{'catalogId':'ep-1','name':'Small','provider':'tag-b','releaseType':'EP'},"
             "{'catalogId':'s-2','name':'Alone','artistName':'Example Artist','provider':'tag-b','releaseType':'single'},"
             "{'catalogId':'x','name':'No provider','releaseType':'album'}],"
             "'relatedArtists':[{'catalogId':'artist-2','name':'Related Artist','provider':'tag-b'},"
             "{'sourceUrl':'https://music.example.test/artists/artist-3','name':'By Address','provider':'tag-b'}],"
             "'stale':false,'fetchedAt':'2026-08-23T10:30:00Z'}"),
        identity);
    QVERIFY(result.isOk());
    const catalog::Detail& artist = result.value();
    QCOMPARE(artist.item.title, QStringLiteral("Example Artist"));
    QCOMPARE(artist.item.catalogId, QStringLiteral("artist-1"));
    QCOMPARE(artist.item.description, QStringLiteral("Example biography"));
    QCOMPARE(artist.item.genreNames.size(), 2);
    QCOMPARE(artist.sections.size(), 4);
    const catalog::Shelf& top = artist.sections.at(0);
    QCOMPARE(top.id, QStringLiteral("top-songs"));
    QCOMPARE(top.items.size(), 1);
    QCOMPARE(top.items.at(0).catalogId, QStringLiteral("song-1"));
    QCOMPARE(top.items.at(0).track->albumCatalogId, QStringLiteral("album-1"));
    QCOMPARE(top.items.at(0).track->artistCatalogId, QStringLiteral("artist-1"));
    // The declared release type decides the section, not the title.
    const catalog::Shelf& albums = artist.sections.at(1);
    QCOMPARE(albums.items.size(), 2);
    QVERIFY(albums.items.at(0).collectionTarget.has_value());
    QCOMPARE(albums.items.at(0).collectionTarget->catalogId, QStringLiteral("album-1"));
    const catalog::Shelf& singles = artist.sections.at(2);
    QCOMPARE(singles.items.size(), 2);
    QCOMPARE(singles.items.at(0).releaseKind, catalog::ReleaseKind::Ep);
    QCOMPARE(singles.items.at(0).subtitle, QStringLiteral("EP"));
    QCOMPARE(singles.items.at(1).subtitle, QStringLiteral("Single · Example Artist"));
    const catalog::Shelf& similar = artist.sections.at(3);
    QCOMPARE(similar.items.size(), 2);
    // An artist known only by address uses it as its id.
    QCOMPARE(similar.items.at(1).catalogId, QStringLiteral("https://music.example.test/artists/artist-3"));
    QVERIFY(similar.items.at(1).artistTarget->identity.catalogId.isEmpty());

    // The name falls back to the identity's; without either it fails.
    QCOMPARE(catalog::parseProviderArtistDetail(json("{}"), identity).value().item.title, QStringLiteral("Fallback Name"));
    QVERIFY(!catalog::parseProviderArtistDetail(json("{}"), LibraryArtistIdentity{}).isOk());
}

void CatalogDetailTest::providerArtistSectionIsChecked()
{
    const lmp::core::Result<catalog::Shelf> similar = catalog::parseProviderArtistSection(
        json("{'resourceType':'artist','items':[{'catalogId':'a','name':'A','provider':'tag-b'}]}"),
        catalog::ArtistSection::SimilarArtists, QStringLiteral("Similar Artists"));
    QVERIFY(similar.isOk());
    QCOMPARE(similar.value().items.size(), 1);
    QCOMPARE(similar.value().id, QStringLiteral("similar-artists"));
    QVERIFY(!catalog::parseProviderArtistSection(json("{'resourceType':'song','items':[]}"), catalog::ArtistSection::Albums,
                                                 QStringLiteral("Albums"))
                 .isOk());
    const catalog::Shelf singles =
        catalog::parseProviderArtistSection(json("{'items':[{'catalogId':'s','name':'S','provider':'tag-b','releaseType':'single'},"
                                                 "{'catalogId':'a','name':'A','provider':'tag-b','releaseType':'album'}]}"),
                                            catalog::ArtistSection::Singles, QStringLiteral("Singles & EPs"))
            .value();
    QCOMPARE(singles.items.size(), 1);
    QCOMPARE(singles.items.at(0).releaseKind, catalog::ReleaseKind::Single);
}

void CatalogDetailTest::providerAlbumIsRead()
{
    const LibraryArtistIdentity identity{{}, {}, QStringLiteral("First Album"), QStringLiteral("tag-b")};
    const lmp::core::Result<catalog::Detail> result = catalog::parseProviderAlbumDetail(
        json("{'release':{'catalogId':'album-1','name':'First Album','artistName':'Example Artist','provider':'tag-b',"
             "'sourceUrl':'https://music.example.test/albums/album-1','artworkUrl':'https://img.example.test/album-1',"
             "'releaseType':'album','releaseDate':'2026-01-02','trackCount':1},"
             "'tracks':[{'id':'track-1','catalogId':'song-1','title':'First Song','artist':'Example Artist',"
             "'album':'First Album','provider':'tag-b','sourceUrl':'https://music.example.test/watch/track-1'}],"
             "'stale':true}"),
        identity);
    QVERIFY(result.isOk());
    QCOMPARE(result.value().item.subtitle, QStringLiteral("Example Artist"));
    QCOMPARE(result.value().item.releaseDate, QStringLiteral("2026-01-02"));
    QVERIFY(result.value().item.collectionTarget.has_value());
    QCOMPARE(result.value().item.collectionTarget->catalogId, QStringLiteral("album-1"));
    QCOMPARE(result.value().tracks.size(), 1);

    // Known only by address: the address is its id, and the target keeps no id.
    const catalog::Detail byAddress =
        catalog::parseProviderAlbumDetail(json("{'release':{'sourceUrl':'https://music.example.test/albums/b'},'tracks':[]}"),
                                          identity)
            .value();
    QCOMPARE(byAddress.item.catalogId, QStringLiteral("https://music.example.test/albums/b"));
    QVERIFY(byAddress.item.collectionTarget->catalogId.isEmpty());
    QVERIFY(!catalog::parseProviderAlbumDetail(json("{'release':{}}"), identity).isOk());
}

void CatalogDetailTest::externalPlaylistIsRead()
{
    const QString id = QStringLiteral("XPabcdefghij12");
    const lmp::core::Result<catalog::Detail> result = catalog::parseExternalPlaylistDetail(
        json("{'playlist':{'title':'Hits','subtitle':'Curator'},"
             "'tracks':[{'id':'tag-b:v1','provider':'tag-b','title':'Song','sourceUrl':'https://music.example.test/watch/v1'}]}"),
        id, testTags());
    QVERIFY(result.isOk());
    QCOMPARE(result.value().item.externalPlaylistId, id);
    QCOMPARE(result.value().item.subtitle, QStringLiteral("Curator"));
    QCOMPARE(result.value().tracks.size(), 1);
    QVERIFY(!catalog::parseExternalPlaylistDetail(json("{'playlist':{'catalogId':'XPother123456','title':'Hits'}}"), id,
                                                  testTags())
                 .isOk());
    QVERIFY(!catalog::parseExternalPlaylistDetail(json("{'playlist':{'title':'Hits'}}"), QStringLiteral("bad"), testTags())
                 .isOk());
}

void CatalogDetailTest::genresFold()
{
    QCOMPARE(catalog::normalizeGenreName(QStringLiteral(" Hip-Hop/Rap ")), QStringLiteral("hip hop rap"));
    QCOMPARE(catalog::normalizeGenreName(QStringLiteral("R&B/Soul")), QStringLiteral("r and b soul"));
    QCOMPARE(catalog::normalizeGenreName(QStringLiteral("Músicà")), QStringLiteral("musica"));
    QCOMPARE(catalog::genreForName(QStringLiteral("Hip-Hop/Rap")), QStringLiteral("Hip-Hop"));
    QCOMPARE(catalog::genreForName(QStringLiteral("Indian Pop")), QStringLiteral("Indian Pop"));
    QCOMPARE(catalog::genreForName(QStringLiteral("Christian Rock")), QStringLiteral("Devotional"));
    QCOMPARE(catalog::genreForName(QStringLiteral("Dancehall")), QStringLiteral("Reggae"));
    QCOMPARE(catalog::genreForName(QStringLiteral("R&B/Soul")), QStringLiteral("R&B"));
    QCOMPARE(catalog::genreForName(QStringLiteral("Singer/Songwriter")), QStringLiteral("Pop"));
    QVERIFY(catalog::genreForName(QStringLiteral("Worldwide")).isEmpty());
    QVERIFY(catalog::genreForName(QStringLiteral("Spoken Word")).isEmpty());
    // Meta names give way to the next name.
    QCOMPARE(catalog::canonicalGenre({QStringLiteral("Music"), QStringLiteral("Regional Indian"), QStringLiteral("Tamil")}),
             QStringLiteral("Tamil"));
    QVERIFY(catalog::canonicalGenre({}).isEmpty());
    QCOMPARE(catalog::musicGenres().size(), 37);
}

QTEST_GUILESS_MAIN(CatalogDetailTest)
#include "tst_catalog_detail.moc"
