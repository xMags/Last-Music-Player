#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonValue>

#include "core/catalog/CatalogModels.h"

namespace lmp::test {

// JSON written with single quotes, so fixtures read without escapes.
inline QByteArray json(const char* text)
{
    QByteArray bytes(text);
    bytes.replace('\'', '"');
    return bytes;
}

inline QJsonValue jsonValue(const char* text)
{
    return QJsonDocument::fromJson(json(text)).object();
}

// Made-up source words, as the private build would supply real ones.
inline core::catalog::SourceTags testTags()
{
    return core::catalog::SourceTags{QStringLiteral("tag-a"),
                                     {QStringLiteral("tag-b")},
                                     {QStringLiteral("label a"), QStringLiteral("label-a")},
                                     QStringLiteral("XP")};
}

// A discovery answer for region "in", ported from the earlier fixture with
// neutral values.
inline QByteArray discoveryFixture()
{
    return json(
        "{'storefront':'in','storefrontName':'India','shelves':["
        "{'id':'top-songs','title':'Top Songs','resourceType':'song',"
        "'chart':{'kind':'global','id':'top-songs','resourceType':'song'},'items':["
        "{'id':'tag-a:1811023667','catalogId':'1811023667','resourceType':'song','provider':'tag-a',"
        "'title':'Sample Song','artistName':'Sample Artist','albumName':'Sample Album',"
        "'albumCatalogId':'1811023660','artistCatalogId':'1440817000','durationMs':215000,"
        "'artworkUrl':'https://cdn.example.test/artwork/song.jpg',"
        "'sourceUrl':'https://catalog.example.test/albums/sample-album/tracks/1811023667',"
        "'releaseDate':'2026-01-09','genreNames':['Pop','Indian Pop']},"
        "{'catalogId':'9','resourceType':'song','title':'','artistName':'Someone'},"
        "{'resourceType':'song','title':'No id'}]},"
        "{'id':'top-playlists','title':'Top Playlists','resourceType':'playlist',"
        "'chart':{'kind':'global','id':'top-playlists','resourceType':'playlist'},'items':["
        "{'catalogId':'pl.abc123','resourceType':'playlist','name':'Sample Playlist',"
        "'curatorName':'Example Curator','artworkUrl':'https://cdn.example.test/artwork/playlist.jpg',"
        "'sourceUrl':'https://catalog.example.test/playlists/pl.abc123','description':'A sample playlist.'}]},"
        "{'id':'empty-shelf','title':'Nothing Here','resourceType':'album','items':[]}],"
        "'charts':["
        "{'kind':'global','id':'top-songs','name':'India','title':'Top Songs in India','subtitle':'Current music region','resourceType':'song'},"
        "{'kind':'genre','id':'14','name':'Pop','title':'Top Pop Songs','subtitle':'Genre chart','resourceType':'song'},"
        "{'kind':'city','id':'city-mumbai','name':'Mumbai','title':'Top Songs in Mumbai','subtitle':'Mumbai city chart','resourceType':'song'},"
        "{'kind':'nearby','id':'unsafe','name':'Ignored','title':'Ignored','resourceType':'song'}],"
        "'cityChartGroups':{"
        "'indian':{'charts':["
        "{'storefront':'in','storefrontName':'India','kind':'city','id':'city-mumbai','name':'Mumbai','title':'Top Songs in Mumbai','resourceType':'song','artworkUrl':'https://cdn.example.test/mumbai.jpg'},"
        "{'kind':'city','id':'city-pune','name':'Pune','title':'Top Songs in Pune','resourceType':'song'}],'partial':false},"
        "'international':{'charts':["
        "{'storefront':'gb','storefrontName':'United Kingdom','kind':'city','id':'city-london','name':'London','title':'Top Songs in London','resourceType':'song'}],'partial':true}},"
        "'moodActivityShelf':{'id':'moods-activities','title':'Moods & Activities','resourceType':'playlist','items':["
        "{'catalogId':'mood-focus','resourceType':'playlist','name':'Pure Focus','curatorName':'Label A'},"
        "{'catalogId':'mood-run','resourceType':'playlist','name':'Run','curatorName':'Label A Fitness'}]},"
        "'stale':true}");
}

} // namespace lmp::test
