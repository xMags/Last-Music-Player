#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"

#include <QByteArray>
#include <QFuture>
#include <QString>
#include <QJsonObject>
#include <QUrlQuery>

namespace lmp::core::account {
class AccountClient;
}

// The catalogue's calls on the account service. Every call goes through
// AccountClient::send, so it reaches the configured API origin only, with the
// session given, and every value in a path or query is percent-encoded.
// Answers come back as sent; core/catalog's parsers read them. A call whose
// input is invalid sends nothing and fails with Unknown.
//
// Holds no state; the AccountClient must outlive it. Futures finish on the
// transport's thread.
namespace lmp::core::catalog {

// How many items a chart's first page and its later pages ask for.
inline constexpr int ChartFirstPage = 100;
inline constexpr int CatalogMorePage = 50;
// A New Songs or New Albums page.
inline constexpr int NewReleasesPage = 20;
// A provider artist section read in full.
inline constexpr int ProviderSectionLimit = 100;

enum class NewReleasesKind {
    Songs,
    Albums,
};

class CatalogClient final {
public:
    explicit CatalogClient(account::AccountClient& client);

    // GET /v1/music/catalog/storefronts
    [[nodiscard]] QFuture<Result<QByteArray>> regions(const QString& bearer);
    // GET /v1/music/catalog/discovery?storefront=<region>&includeCityChartGroups=true
    [[nodiscard]] QFuture<Result<QByteArray>> discovery(const QString& bearer, const QString& region);
    // GET /v1/music/catalog/charts?storefront&type=<type>s&limit[&cityId|&genreId][&offset]
    // `limit` is held to 1..100; an offset of 0 or less is left out.
    [[nodiscard]] QFuture<Result<QByteArray>> chart(const QString& bearer, const ChartRef& chart, int offset, int limit);
    // GET /v1/music/catalog/<type>s/<id>?storefront=<region>
    [[nodiscard]] QFuture<Result<QByteArray>> detail(const QString& bearer, const QString& region, ResourceType type,
                                                     const QString& catalogId);
    // GET /v1/music/catalog/artists/<id>/view/<view>?storefront&limit[&offset]
    [[nodiscard]] QFuture<Result<QByteArray>> catalogArtistSection(const QString& bearer, const QString& region,
                                                                   const QString& artistId, ArtistSection section,
                                                                   int offset, int limit);
    // GET /v1/music/catalog/artists/detail?<one identity>
    [[nodiscard]] QFuture<Result<QByteArray>> providerArtist(const QString& bearer,
                                                             const account::LibraryArtistIdentity& identity);
    // GET /v1/music/catalog/artists/detail/view/<section>?<one identity>&limit=100
    [[nodiscard]] QFuture<Result<QByteArray>> providerArtistSection(const QString& bearer,
                                                                    const account::LibraryArtistIdentity& identity,
                                                                    ArtistSection section);
    // GET /v1/music/catalog/albums/detail?<one identity>[&artist]
    [[nodiscard]] QFuture<Result<QByteArray>> providerAlbum(const QString& bearer,
                                                            const account::LibraryArtistIdentity& identity,
                                                            const QString& artist);
    // GET /v1/music/catalog/playlists/detail?id=<external playlist id>
    [[nodiscard]] QFuture<Result<QByteArray>> externalPlaylist(const QString& bearer, const QString& id,
                                                               const SourceTags& tags);
    // GET /v1/music/catalog/new-releases?storefront&kind&offset&limit=20
    [[nodiscard]] QFuture<Result<QByteArray>> newReleases(const QString& bearer, const QString& region,
                                                          NewReleasesKind kind, int offset);
    // GET /v1/music/catalog/artists/resolve?storefront&name
    [[nodiscard]] QFuture<Result<QByteArray>> resolveArtist(const QString& bearer, const QString& region,
                                                            const QString& name);
    // GET /v1/music/catalog/songs/album?storefront&<song fields>: the album a
    // song belongs to. `fields` holds the song's non-empty fields.
    [[nodiscard]] QFuture<Result<QByteArray>> songAlbum(const QString& bearer, const QString& region,
                                                        const QUrlQuery& fields);
    // POST /v1/music/preferences/languages/infer: detects the account's
    // languages from its listening (rate limited; can take seconds).
    [[nodiscard]] QFuture<Result<QByteArray>> inferLanguages(const QString& bearer);
    // PUT /v1/music/preferences {"contentLanguages": <setting>}: the answer
    // is the stored preferences.
    [[nodiscard]] QFuture<Result<QByteArray>> saveLanguages(const QString& bearer, const QJsonObject& setting);
    // GET /v1/music/search?q=
    [[nodiscard]] QFuture<Result<QByteArray>> search(const QString& bearer, const QString& query);
    // GET /v1/music/search/all?q=
    [[nodiscard]] QFuture<Result<QByteArray>> searchAll(const QString& bearer, const QString& query);

private:
    [[nodiscard]] QFuture<Result<QByteArray>> get(const QString& bearer, const QString& pathAndQuery);

    account::AccountClient& m_client;
};

// The view name the service uses for an artist section ("full-albums" for
// albums).
[[nodiscard]] QString artistSectionView(ArtistSection section);

} // namespace lmp::core::catalog
