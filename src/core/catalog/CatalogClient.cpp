#include "core/catalog/CatalogClient.h"

#include "core/account/AccountClient.h"
#include "core/catalog/CatalogDetailParser.h"

#include <QJsonDocument>
#include <QUrl>

#include <chrono>

#include <algorithm>
#include <optional>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] QFuture<Result<QByteArray>> refused(const QString& detail)
{
    return QtFuture::makeReadyValueFuture(Result<QByteArray>(Failure::of(FailureKind::Unknown, detail)));
}

[[nodiscard]] QString encoded(const QString& value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

// "key=value&..." with every key and value percent-encoded, in order.
[[nodiscard]] QString queryText(const QList<std::pair<QString, QString>>& items)
{
    QString text;
    for (const auto& [key, value] : items) {
        if (!text.isEmpty()) {
            text.append(QLatin1Char('&'));
        }
        text.append(encoded(key) + QLatin1Char('=') + encoded(value));
    }
    return text;
}

[[nodiscard]] QString identityText(const QUrlQuery& query)
{
    QList<std::pair<QString, QString>> items;
    for (const auto& [key, value] : query.queryItems(QUrl::FullyDecoded)) {
        items.append({key, value});
    }
    return queryText(items);
}

} // namespace

QString artistSectionView(ArtistSection section)
{
    return section == ArtistSection::Albums ? QStringLiteral("full-albums") : artistSectionId(section);
}

CatalogClient::CatalogClient(account::AccountClient& client)
    : m_client(client)
{
}

QFuture<Result<QByteArray>> CatalogClient::get(const QString& bearer, const QString& pathAndQuery)
{
    account::AccountCall call;
    call.method = net::HttpMethod::Get;
    call.pathAndQuery = pathAndQuery;
    call.bearerSession = bearer;
    return m_client.send(call).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
        if (!response) {
            return response.failure();
        }
        return response.value().body;
    });
}

QFuture<Result<QByteArray>> CatalogClient::regions(const QString& bearer)
{
    return get(bearer, QStringLiteral("/v1/music/catalog/storefronts"));
}

QFuture<Result<QByteArray>> CatalogClient::discovery(const QString& bearer, const QString& region)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    if (!code) {
        return refused(QStringLiteral("Invalid music region."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/discovery?storefront=") + *code
                           + QStringLiteral("&includeCityChartGroups=true"));
}

QFuture<Result<QByteArray>> CatalogClient::chart(const QString& bearer, const ChartRef& chart, int offset, int limit)
{
    if (!isValidChartRef(chart)) {
        return refused(QStringLiteral("Invalid catalog chart."));
    }
    QList<std::pair<QString, QString>> items{
        {QStringLiteral("storefront"), chart.region},
        {QStringLiteral("type"), resourceTypePlural(chart.resourceType)},
        {QStringLiteral("limit"), QString::number(std::clamp(limit, 1, 100))},
    };
    if (chart.kind == ChartKind::City) {
        items.append({QStringLiteral("cityId"), chart.id});
    } else if (chart.kind == ChartKind::Genre) {
        items.append({QStringLiteral("genreId"), chart.id});
    }
    if (offset > 0) {
        items.append({QStringLiteral("offset"), QString::number(offset)});
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/charts?") + queryText(items));
}

QFuture<Result<QByteArray>> CatalogClient::detail(const QString& bearer, const QString& region, ResourceType type,
                                                  const QString& catalogId)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    const QString id = catalogId.trimmed();
    if (!code || type == ResourceType::Unknown || id.isEmpty()) {
        return refused(QStringLiteral("Invalid catalog page."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/") + resourceTypePlural(type) + QLatin1Char('/') + encoded(id)
                           + QStringLiteral("?storefront=") + *code);
}

QFuture<Result<QByteArray>> CatalogClient::catalogArtistSection(const QString& bearer, const QString& region,
                                                                const QString& artistId, ArtistSection section,
                                                                int offset, int limit)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    const QString id = artistId.trimmed();
    if (!code || id.isEmpty()) {
        return refused(QStringLiteral("Invalid artist section."));
    }
    QString path = QStringLiteral("/v1/music/catalog/artists/") + encoded(id) + QStringLiteral("/view/")
        + artistSectionView(section) + QStringLiteral("?storefront=") + *code + QStringLiteral("&limit=")
        + QString::number(std::clamp(limit, 1, 100));
    if (offset > 0) {
        path += QStringLiteral("&offset=") + QString::number(offset);
    }
    return get(bearer, path);
}

QFuture<Result<QByteArray>> CatalogClient::providerArtist(const QString& bearer,
                                                          const account::LibraryArtistIdentity& identity)
{
    const std::optional<QUrlQuery> query = detailIdentityQuery(identity);
    if (!query) {
        return refused(QStringLiteral("This artist cannot be opened."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/artists/detail?") + identityText(*query));
}

QFuture<Result<QByteArray>> CatalogClient::providerArtistSection(const QString& bearer,
                                                                 const account::LibraryArtistIdentity& identity,
                                                                 ArtistSection section)
{
    const std::optional<QUrlQuery> query = detailIdentityQuery(identity);
    if (!query) {
        return refused(QStringLiteral("This artist cannot be opened."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/artists/detail/view/") + artistSectionId(section)
                           + QLatin1Char('?') + identityText(*query) + QStringLiteral("&limit=")
                           + QString::number(ProviderSectionLimit));
}

QFuture<Result<QByteArray>> CatalogClient::providerAlbum(const QString& bearer,
                                                         const account::LibraryArtistIdentity& identity,
                                                         const QString& artist)
{
    const std::optional<QUrlQuery> query = detailIdentityQuery(identity, artist);
    if (!query) {
        return refused(QStringLiteral("This album cannot be opened."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/albums/detail?") + identityText(*query));
}

QFuture<Result<QByteArray>> CatalogClient::externalPlaylist(const QString& bearer, const QString& id,
                                                            const SourceTags& tags)
{
    if (!tags.isExternalPlaylistId(id)) {
        return refused(QStringLiteral("Invalid playlist."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/playlists/detail?id=") + encoded(id));
}

QFuture<Result<QByteArray>> CatalogClient::newReleases(const QString& bearer, const QString& region,
                                                       NewReleasesKind kind, int offset)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    if (!code || offset < 0) {
        return refused(QStringLiteral("Invalid new releases page."));
    }
    const QList<std::pair<QString, QString>> items{
        {QStringLiteral("storefront"), *code},
        {QStringLiteral("kind"), kind == NewReleasesKind::Songs ? QStringLiteral("songs") : QStringLiteral("albums")},
        {QStringLiteral("offset"), QString::number(offset)},
        {QStringLiteral("limit"), QString::number(NewReleasesPage)},
    };
    return get(bearer, QStringLiteral("/v1/music/catalog/new-releases?") + queryText(items));
}

QFuture<Result<QByteArray>> CatalogClient::resolveArtist(const QString& bearer, const QString& region,
                                                         const QString& name)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    const QString trimmed = name.trimmed();
    if (!code || trimmed.isEmpty()) {
        return refused(QStringLiteral("Invalid artist."));
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/artists/resolve?storefront=") + *code + QStringLiteral("&name=")
                           + encoded(trimmed));
}

QFuture<Result<QByteArray>> CatalogClient::songAlbum(const QString& bearer, const QString& region,
                                                     const QUrlQuery& fields)
{
    const std::optional<QString> code = normalizeRegionCode(region);
    if (!code || (!fields.hasQueryItem(QStringLiteral("title")) && !fields.hasQueryItem(QStringLiteral("catalogId"))
                  && !fields.hasQueryItem(QStringLiteral("sourceUrl")))) {
        return refused(QStringLiteral("Invalid song."));
    }
    QList<std::pair<QString, QString>> items{{QStringLiteral("storefront"), *code}};
    for (const auto& [key, value] : fields.queryItems(QUrl::FullyDecoded)) {
        items.append({key, value});
    }
    return get(bearer, QStringLiteral("/v1/music/catalog/songs/album?") + queryText(items));
}

QFuture<Result<QByteArray>> CatalogClient::inferLanguages(const QString& bearer)
{
    account::AccountCall call;
    call.method = net::HttpMethod::Post;
    call.pathAndQuery = QStringLiteral("/v1/music/preferences/languages/infer");
    call.bearerSession = bearer;
    call.timeout = std::chrono::seconds(30);
    return m_client.send(call).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
        if (!response) {
            return response.failure();
        }
        return response.value().body;
    });
}

QFuture<Result<QByteArray>> CatalogClient::saveLanguages(const QString& bearer, const QJsonObject& setting)
{
    account::AccountCall call;
    call.method = net::HttpMethod::Put;
    call.pathAndQuery = QStringLiteral("/v1/music/preferences");
    call.bearerSession = bearer;
    call.jsonBody = QJsonDocument(QJsonObject{{QStringLiteral("contentLanguages"), setting}}).toJson(QJsonDocument::Compact);
    return m_client.send(call).then([](const Result<net::HttpResponse>& response) -> Result<QByteArray> {
        if (!response) {
            return response.failure();
        }
        return response.value().body;
    });
}

QFuture<Result<QByteArray>> CatalogClient::artistFollow(const QString& bearer,
                                                       const account::LibraryArtistIdentity& identity)
{
    const QString provider = identity.provider.trimmed();
    const QString catalogId = identity.catalogId.trimmed();
    const QString sourceUrl = identity.sourceUrl.trimmed();
    if (provider.isEmpty() || (catalogId.isEmpty() && sourceUrl.isEmpty())) {
        return refused(QStringLiteral("this artist cannot be followed"));
    }
    QList<std::pair<QString, QString>> items{{QStringLiteral("provider"), provider},
                                             {QStringLiteral("name"), identity.name.trimmed()}};
    if (!catalogId.isEmpty()) {
        items.append({QStringLiteral("catalogId"), catalogId});
    }
    if (!sourceUrl.isEmpty()) {
        items.append({QStringLiteral("sourceUrl"), sourceUrl});
    }
    return get(bearer, QStringLiteral("/v1/music/artist-follow?") + queryText(items));
}

QFuture<Result<QByteArray>> CatalogClient::search(const QString& bearer, const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return refused(QStringLiteral("Empty search."));
    }
    return get(bearer, QStringLiteral("/v1/music/search?q=") + encoded(trimmed));
}

QFuture<Result<QByteArray>> CatalogClient::related(const QString& bearer, const QString& sourceUrl,
                                                   const QString& sourceReference)
{
    const QString link = sourceUrl.trimmed();
    if (link.isEmpty()) {
        return refused(QStringLiteral("This song has no link."));
    }
    const QString reference = sourceReference.trimmed();
    return get(bearer, QStringLiteral("/v1/music/related?")
                           + (reference.isEmpty() ? queryText({{QStringLiteral("sourceUrl"), link}})
                                                  : queryText({{QStringLiteral("sourceRef"), reference}})));
}

QFuture<Result<QByteArray>> CatalogClient::searchAll(const QString& bearer, const QString& query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return refused(QStringLiteral("Empty search."));
    }
    return get(bearer, QStringLiteral("/v1/music/search/all?q=") + encoded(trimmed));
}

} // namespace lmp::core::catalog
