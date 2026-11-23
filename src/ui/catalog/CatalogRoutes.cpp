#include "ui/catalog/CatalogRoutes.h"

#include <QMap>

namespace lmp::ui::catalog_routes {

namespace catalog = core::catalog;

namespace {

[[nodiscard]] QMap<QString, QString> identityParams(const core::account::LibraryArtistIdentity& identity)
{
    QMap<QString, QString> params{{QStringLiteral("provider"), identity.provider},
                                  {QStringLiteral("name"), identity.name}};
    if (!identity.catalogId.isEmpty()) {
        params.insert(QStringLiteral("catalogId"), identity.catalogId);
    }
    if (!identity.sourceUrl.isEmpty()) {
        params.insert(QStringLiteral("sourceUrl"), identity.sourceUrl);
    }
    return params;
}

} // namespace

std::optional<Route> routeForItem(const catalog::CatalogItem& item)
{
    switch (item.type) {
    case catalog::ResourceType::Song:
    case catalog::ResourceType::Unknown:
        return std::nullopt;
    case catalog::ResourceType::Album:
        if (item.collectionTarget) {
            return routeForAlbumIdentity(*item.collectionTarget, {});
        }
        return Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("album")},
                                                  {QStringLiteral("id"), item.catalogId},
                                                  {QStringLiteral("title"), item.title}});
    case catalog::ResourceType::Playlist:
        if (!item.externalPlaylistId.isEmpty()) {
            return Route(Route::Kind::ExternalPlaylist,
                         {{QStringLiteral("id"), item.externalPlaylistId}, {QStringLiteral("title"), item.title}});
        }
        return Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("playlist")},
                                                  {QStringLiteral("id"), item.catalogId},
                                                  {QStringLiteral("title"), item.title}});
    case catalog::ResourceType::Artist:
        if (item.artistTarget) {
            QMap<QString, QString> params = identityParams(item.artistTarget->identity);
            if (!item.artistTarget->catalogArtistId.isEmpty()) {
                params.insert(QStringLiteral("catalogArtistId"), item.artistTarget->catalogArtistId);
            }
            params.insert(QStringLiteral("title"), item.title);
            return Route(Route::Kind::SourceArtist, params);
        }
        return Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("artist")},
                                                  {QStringLiteral("id"), item.catalogId},
                                                  {QStringLiteral("title"), item.title}});
    }
    return std::nullopt;
}

Route routeForChart(const catalog::ChartRef& chart)
{
    return Route(Route::Kind::Chart, {{QStringLiteral("region"), chart.region},
                                      {QStringLiteral("kind"), catalog::chartKindName(chart.kind)},
                                      {QStringLiteral("id"), chart.id},
                                      {QStringLiteral("type"), catalog::resourceTypeName(chart.resourceType)}});
}

std::optional<catalog::ChartRef> chartFromRoute(const Route& route)
{
    const catalog::ChartRef chart{route.param(QStringLiteral("region")),
                                  catalog::parseChartKind(route.param(QStringLiteral("kind"))),
                                  route.param(QStringLiteral("id")),
                                  catalog::parseResourceType(route.param(QStringLiteral("type")))};
    return catalog::isValidChartRef(chart) ? std::optional(chart) : std::nullopt;
}

Route routeForAlbumIdentity(const core::account::LibraryArtistIdentity& identity, const QString& artist)
{
    QMap<QString, QString> params = identityParams(identity);
    if (!artist.isEmpty()) {
        params.insert(QStringLiteral("artist"), artist);
    }
    params.insert(QStringLiteral("title"), identity.name);
    return Route(Route::Kind::SourceAlbum, params);
}

core::account::LibraryArtistIdentity identityFromRoute(const Route& route)
{
    return core::account::LibraryArtistIdentity{route.param(QStringLiteral("catalogId")),
                                                 route.param(QStringLiteral("sourceUrl")),
                                                 route.param(QStringLiteral("name")),
                                                 route.param(QStringLiteral("provider"))};
}

Route routeFindingAlbum(const core::Track& track)
{
    return Route(Route::Kind::SourceAlbum,
                 {{QStringLiteral("finding"), QStringLiteral("1")},
                  {QStringLiteral("provider"), track.sourceTag},
                  {QStringLiteral("catalogId"), track.catalogId},
                  {QStringLiteral("sourceUrl"), track.sourceUrl},
                  {QStringLiteral("title"), track.title},
                  {QStringLiteral("artist"), track.artist},
                  {QStringLiteral("album"), track.album},
                  {QStringLiteral("albumCatalogId"), track.albumCatalogId},
                  {QStringLiteral("durationMs"),
                   track.durationSeconds > 0 ? QString::number(qRound64(track.durationSeconds * 1000)) : QString()}});
}

Route routeForShelf(const QString& key)
{
    return Route(Route::Kind::Shelf, {{QStringLiteral("key"), key}});
}

} // namespace lmp::ui::catalog_routes
