#include "ui/navigation/RouteRules.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <utility>

namespace lmp::ui {

namespace {

using Params = QMap<QString, QString>;

bool matches(const QString& pattern, const QString& value,
             QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption)
{
    const QRegularExpression expression(QRegularExpression::anchoredPattern(pattern), options);
    return expression.match(value).hasMatch();
}

// Text longer than `limit` is refused (empty), never cut.
QString bounded(const QString& value, qsizetype limit)
{
    const QString text = value.trimmed();
    return text.size() <= limit ? text : QString();
}

// Only an https link without user name or password.
QString safeHttpsUrl(const QString& value)
{
    const QString raw = bounded(value, 4096);
    if (raw.isEmpty()) {
        return {};
    }
    const QUrl url(raw, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QLatin1String("https") || !url.userInfo().isEmpty()) {
        return {};
    }
    return url.toString(QUrl::FullyEncoded);
}

bool isResourceType(const QString& type)
{
    static const QStringList types{QStringLiteral("song"), QStringLiteral("album"),
                                   QStringLiteral("playlist"), QStringLiteral("artist")};
    return types.contains(type);
}

bool isChartKind(const QString& kind)
{
    return kind == QLatin1String("global") || kind == QLatin1String("city")
        || kind == QLatin1String("genre");
}

// Letters, digits, underscore, dot and dash; at most 160 characters.
bool validScopeId(const QString& value)
{
    return !value.isEmpty() && value.size() <= 160 && matches(QStringLiteral("[\\w.-]+"), value);
}

// Fills `params` with a checked identity. A release may be named by id alone;
// an artist always needs its name.
bool identityParams(const SourceIdentity& identity, bool requireName, Params& params)
{
    const QString name = bounded(identity.name, 240);
    const QString sourceTag = bounded(identity.sourceTag, 64);
    const QString catalogId = bounded(identity.catalogId, 512);
    const QString sourceUrl = safeHttpsUrl(identity.sourceUrl);
    const QString artist = bounded(identity.artist, 240);
    if (sourceTag.isEmpty()) {
        return false;
    }
    if (requireName ? name.isEmpty() : name.isEmpty() && catalogId.isEmpty() && sourceUrl.isEmpty()) {
        return false;
    }
    params.insert(QStringLiteral("name"), name);
    params.insert(QStringLiteral("sourceTag"), sourceTag);
    const std::pair<const char*, QString> optional[] = {
        {"catalogId", catalogId}, {"sourceUrl", sourceUrl}, {"artist", artist}};
    for (const auto& [key, value] : optional) {
        if (!value.isEmpty()) {
            params.insert(QLatin1String(key), value);
        }
    }
    return true;
}

// The optional numeric catalogue artist id: digits only, at most 64.
bool catalogArtistParam(const QString& value, Params& params)
{
    const QString id = bounded(value, 64);
    if (id.isEmpty()) {
        return true;
    }
    if (!matches(QStringLiteral("\\d+"), id)) {
        return false;
    }
    params.insert(QStringLiteral("catalogArtistId"), id);
    return true;
}

} // namespace

RouteRules::RouteRules(QString externalPlaylistPrefix)
    : m_externalPlaylistPrefix(std::move(externalPlaylistPrefix))
{
}

std::optional<Route> RouteRules::catalogDetail(const QString& type, const QString& catalogId) const
{
    const QString id = catalogId.trimmed();
    if (!isResourceType(type) || id.isEmpty()) {
        return std::nullopt;
    }
    return Route(Route::Kind::CatalogDetail,
                 {{QStringLiteral("type"), type}, {QStringLiteral("catalogId"), id}});
}

std::optional<Route> RouteRules::libraryArtist(const QString& publicId) const
{
    const QString id = publicId.trimmed();
    if (!matches(QStringLiteral("mar_[0-9a-f-]{36}"), id,
                 QRegularExpression::CaseInsensitiveOption)) {
        return std::nullopt;
    }
    return Route(Route::Kind::LibraryArtist, {{QStringLiteral("publicId"), id}});
}

std::optional<Route> RouteRules::sourceArtist(const SourceIdentity& identity,
                                              const QString& catalogArtistId) const
{
    Params params;
    if (!identityParams(identity, true, params) || !catalogArtistParam(catalogArtistId, params)) {
        return std::nullopt;
    }
    return Route(Route::Kind::SourceArtist, params);
}

std::optional<Route> RouteRules::sourceAlbum(const SourceIdentity& identity) const
{
    Params params;
    if (!identityParams(identity, false, params)) {
        return std::nullopt;
    }
    return Route(Route::Kind::SourceAlbum, params);
}

std::optional<Route> RouteRules::externalPlaylist(const QString& playlistId) const
{
    if (m_externalPlaylistPrefix.isEmpty()) {
        return std::nullopt;
    }
    const QString pattern =
        QRegularExpression::escape(m_externalPlaylistPrefix) + QStringLiteral("[A-Za-z0-9_-]{10,80}");
    if (!matches(pattern, playlistId)) {
        return std::nullopt;
    }
    return Route(Route::Kind::ExternalPlaylist, {{QStringLiteral("playlistId"), playlistId}});
}

std::optional<Route> RouteRules::shared(const QString& code) const
{
    if (!matches(QStringLiteral("[A-Za-z0-9]{10}"), code)) {
        return std::nullopt;
    }
    return Route(Route::Kind::Shared, {{QStringLiteral("code"), code}});
}

std::optional<Route> RouteRules::artistSection(const SourceIdentity& identity,
                                               const QString& catalogArtistId,
                                               const QString& sectionId,
                                               const QString& region) const
{
    static const QStringList sections{QStringLiteral("top-songs"), QStringLiteral("albums"),
                                      QStringLiteral("singles"), QStringLiteral("similar-artists")};
    Params params;
    if (!sections.contains(sectionId) || !identityParams(identity, true, params)
        || !catalogArtistParam(catalogArtistId, params)) {
        return std::nullopt;
    }
    params.insert(QStringLiteral("sectionId"), sectionId);
    // An unusable region is left out, not refused.
    const QString code = normalizeRegion(region);
    if (!code.isEmpty()) {
        params.insert(QStringLiteral("region"), code);
    }
    return Route(Route::Kind::ArtistSection, params);
}

std::optional<Route> RouteRules::shelf(const QString& shelfId) const
{
    static const QStringList shelves{QStringLiteral("moods-activities"),
                                     QStringLiteral("new-release-songs"),
                                     QStringLiteral("new-releases"), QStringLiteral("biggest-hits"),
                                     QStringLiteral("popular-artists")};
    const QString id = shelfId.trimmed();
    if (!shelves.contains(id)) {
        return std::nullopt;
    }
    return Route(Route::Kind::Shelf, {{QStringLiteral("shelfId"), id}});
}

std::optional<Route> RouteRules::charts(const QString& section) const
{
    const QString id = section.trimmed();
    if (id != QLatin1String("explore") && id != QLatin1String("cities")) {
        return std::nullopt;
    }
    return Route(Route::Kind::Charts, {{QStringLiteral("section"), id}});
}

std::optional<Route> RouteRules::chart(const QString& type, const QString& scopeKind,
                                       const QString& scopeId, const QString& region) const
{
    const QString id = scopeId.trimmed();
    if (!isResourceType(type) || type == QLatin1String("artist") || !isChartKind(scopeKind)
        || !validScopeId(id)) {
        return std::nullopt;
    }
    Params params{{QStringLiteral("type"), type},
                  {QStringLiteral("scopeKind"), scopeKind},
                  {QStringLiteral("scopeId"), id}};
    if (!region.isEmpty()) {
        // Only a song chart of a city carries a region, and it must be valid.
        const QString code = normalizeRegion(region);
        if (code.isEmpty() || scopeKind != QLatin1String("city") || type != QLatin1String("song")) {
            return std::nullopt;
        }
        params.insert(QStringLiteral("region"), code);
        return Route(Route::Kind::Chart, params);
    }
    const bool songOnly = scopeKind == QLatin1String("city") || scopeKind == QLatin1String("genre");
    if (songOnly && type != QLatin1String("song")) {
        return std::nullopt;
    }
    if (scopeKind == QLatin1String("global") && id != QStringLiteral("top-%1s").arg(type)) {
        return std::nullopt;
    }
    return Route(Route::Kind::Chart, params);
}

QString RouteRules::normalizeRegion(const QString& value)
{
    const QString code = value.trimmed().toLower();
    return matches(QStringLiteral("[a-z]{2}"), code) ? code : QString();
}

} // namespace lmp::ui
