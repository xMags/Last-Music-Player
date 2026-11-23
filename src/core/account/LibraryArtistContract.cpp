#include "core/account/LibraryArtistContract.h"

#include "core/net/ArtworkUrl.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmp::core::account {

namespace {

constexpr qsizetype kMaxPublicIdCharacters = 256;
constexpr qsizetype kMaxProviderCharacters = 80;
constexpr qsizetype kMaxCatalogIdCharacters = 512;
constexpr qsizetype kMaxNameCharacters = 512;
constexpr qsizetype kMaxUrlCharacters = 4096;
constexpr qsizetype kMaxCursorCharacters = 2048;

enum class Presence { Optional, Required };

// A text field: missing (or null, when optional) reads as empty; anything
// that is not text, or text longer than `maximum`, or a required field that
// is empty, is refused.
[[nodiscard]] std::optional<QString> boundedString(const QJsonObject& object, QLatin1StringView key, qsizetype maximum,
                                                   Presence presence = Presence::Optional)
{
    const bool required = presence == Presence::Required;
    if (!object.contains(key)) {
        return required ? std::nullopt : std::optional<QString>(QString());
    }
    const QJsonValue value = object.value(key);
    if (!required && value.isNull()) {
        return QString();
    }
    if (!value.isString()) {
        return std::nullopt;
    }
    QString result = value.toString();
    if (result.size() > maximum || (required && result.isEmpty())) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::optional<QJsonObject> objectValue(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isObject() ? std::optional<QJsonObject>(value.toObject()) : std::nullopt;
}

[[nodiscard]] std::optional<bool> booleanValue(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isBool() ? std::optional<bool>(value.toBool()) : std::nullopt;
}

// A whole number of at least 0. Missing reads as 0 unless required.
[[nodiscard]] std::optional<qint64> countValue(const QJsonObject& object, QLatin1StringView key, Presence presence)
{
    if (!object.contains(key)) {
        return presence == Presence::Required ? std::nullopt : std::optional<qint64>(0);
    }
    const QJsonValue value = object.value(key);
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0
        || number > static_cast<double>(std::numeric_limits<qint64>::max()) || std::floor(number) != number) {
        return std::nullopt;
    }
    return static_cast<qint64>(number);
}

[[nodiscard]] bool safeOptionalUrl(const QString& value)
{
    return value.isEmpty()
        || (value.size() <= kMaxUrlCharacters && net::isSafeRemoteUrl(value, net::RemoteUrlUse::Durable));
}

// Artwork may also be an account cover link.
[[nodiscard]] bool safeOptionalArtwork(const QString& value)
{
    return !net::accountCoverLinkPath(value).isEmpty() || safeOptionalUrl(value);
}

[[nodiscard]] std::optional<LibraryArtist> parseArtist(const QJsonObject& object)
{
    std::optional<QString> id = boundedString(object, QLatin1StringView("id"), kMaxPublicIdCharacters, Presence::Required);
    std::optional<QString> name = boundedString(object, QLatin1StringView("name"), kMaxNameCharacters, Presence::Required);
    std::optional<QString> provider = boundedString(object, QLatin1StringView("provider"), kMaxProviderCharacters);
    std::optional<QString> providerCatalogId =
        boundedString(object, QLatin1StringView("providerCatalogId"), kMaxCatalogIdCharacters);
    std::optional<QString> providerSourceUrl =
        boundedString(object, QLatin1StringView("providerSourceUrl"), kMaxUrlCharacters);

    if (const std::optional<QJsonObject> identity = objectValue(object, QLatin1StringView("providerIdentity"))) {
        std::optional<QString> nestedProvider =
            boundedString(*identity, QLatin1StringView("provider"), kMaxProviderCharacters, Presence::Required);
        std::optional<QString> nestedCatalogId =
            boundedString(*identity, QLatin1StringView("catalogId"), kMaxCatalogIdCharacters);
        std::optional<QString> nestedSourceUrl =
            boundedString(*identity, QLatin1StringView("sourceUrl"), kMaxUrlCharacters);
        if (!nestedProvider || !nestedCatalogId || !nestedSourceUrl) {
            return std::nullopt;
        }
        provider = std::move(nestedProvider);
        providerCatalogId = std::move(nestedCatalogId);
        providerSourceUrl = std::move(nestedSourceUrl);
    } else {
        if (providerCatalogId && providerCatalogId->isEmpty()) {
            providerCatalogId = boundedString(object, QLatin1StringView("catalogId"), kMaxCatalogIdCharacters);
        }
        if (providerSourceUrl && providerSourceUrl->isEmpty()) {
            providerSourceUrl = boundedString(object, QLatin1StringView("sourceUrl"), kMaxUrlCharacters);
        }
    }

    std::optional<QString> artworkUrl = boundedString(object, QLatin1StringView("artworkUrl"), kMaxUrlCharacters);
    std::optional<QString> heroArtworkUrl = boundedString(object, QLatin1StringView("heroArtworkUrl"), kMaxUrlCharacters);
    const std::optional<qint64> trackCount = countValue(object, QLatin1StringView("trackCount"), Presence::Required);
    const std::optional<qint64> albumCount = countValue(object, QLatin1StringView("albumCount"), Presence::Required);
    const std::optional<qint64> playCount = countValue(object, QLatin1StringView("playCount"), Presence::Required);
    const std::optional<bool> followed = booleanValue(object, QLatin1StringView("followed"));
    const std::optional<bool> resolving = booleanValue(object, QLatin1StringView("resolving"));
    if (!id || !name || !provider || provider->isEmpty() || !providerCatalogId || !providerSourceUrl || !artworkUrl
        || !heroArtworkUrl || !trackCount || !albumCount || !playCount || !followed || !resolving
        || !isValidLibraryArtistPublicId(*id) || !safeOptionalUrl(*providerSourceUrl)
        || !safeOptionalArtwork(*artworkUrl) || !safeOptionalArtwork(*heroArtworkUrl)) {
        return std::nullopt;
    }

    LibraryArtist artist;
    artist.detailIdentity = {*providerCatalogId, *providerSourceUrl, *name, *provider};
    if (const std::optional<QJsonObject> detail = objectValue(object, QLatin1StringView("detailIdentity"))) {
        std::optional<QString> catalogId = boundedString(*detail, QLatin1StringView("catalogId"), kMaxCatalogIdCharacters);
        std::optional<QString> sourceUrl = boundedString(*detail, QLatin1StringView("sourceUrl"), kMaxUrlCharacters);
        std::optional<QString> detailName = boundedString(*detail, QLatin1StringView("name"), kMaxNameCharacters);
        std::optional<QString> detailProvider =
            boundedString(*detail, QLatin1StringView("provider"), kMaxProviderCharacters);
        if (!catalogId || !sourceUrl || !detailName || !detailProvider || !safeOptionalUrl(*sourceUrl)) {
            return std::nullopt;
        }
        if (!catalogId->isEmpty()) {
            artist.detailIdentity.catalogId = *catalogId;
        }
        if (!sourceUrl->isEmpty()) {
            artist.detailIdentity.sourceUrl = *sourceUrl;
        }
        if (!detailName->isEmpty()) {
            artist.detailIdentity.name = *detailName;
        }
        if (!detailProvider->isEmpty()) {
            artist.detailIdentity.provider = *detailProvider;
        }
    }
    artist.id = *id;
    artist.name = *name;
    artist.provider = *provider;
    artist.providerCatalogId = *providerCatalogId;
    artist.providerSourceUrl = *providerSourceUrl;
    artist.artworkUrl = net::durableAccountArtworkUrl(*artworkUrl);
    artist.heroArtworkUrl = net::durableAccountArtworkUrl(*heroArtworkUrl);
    artist.trackCount = *trackCount;
    artist.albumCount = *albumCount;
    artist.playCount = *playCount;
    artist.followed = *followed;
    artist.resolving = *resolving;
    return artist;
}

} // namespace

QLatin1StringView libraryArtistViewName(LibraryArtistView view)
{
    switch (view) {
    case LibraryArtistView::Followed:
        return QLatin1StringView("followed");
    case LibraryArtistView::All:
        return QLatin1StringView("all");
    case LibraryArtistView::Default:
        break;
    }
    return QLatin1StringView("default");
}

bool isValidLibraryArtistPublicId(const QString& id)
{
    if (id.isEmpty() || id.size() > kMaxPublicIdCharacters) {
        return false;
    }
    return std::none_of(id.cbegin(), id.cend(), [](QChar character) {
        const char16_t unit = character.unicode();
        return unit < u' ' || unit == 0x7f;
    });
}

std::optional<QString> libraryArtistListPath(const LibraryArtistListRequest& request)
{
    const QString query = request.query.trimmed();
    if (query.size() > MaxLibraryArtistSearchCharacters || request.limit <= 0
        || request.limit > MaxLibraryArtistPageSize || request.cursor.size() > kMaxCursorCharacters) {
        return std::nullopt;
    }
    QString path = QStringLiteral("/v1/music/library/artists?view=") + libraryArtistViewName(request.view)
        + QStringLiteral("&limit=") + QString::number(request.limit);
    if (!query.isEmpty()) {
        path += QStringLiteral("&q=") + QString::fromLatin1(QUrl::toPercentEncoding(query));
    }
    if (!request.cursor.isEmpty()) {
        path += QStringLiteral("&cursor=") + QString::fromLatin1(QUrl::toPercentEncoding(request.cursor));
    }
    return path;
}

LibraryArtistPage parseLibraryArtistPage(const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    QJsonObject root = document.object();
    if (const std::optional<QJsonObject> data = objectValue(root, QLatin1StringView("data"))) {
        root = *data;
    }
    const QJsonValue artistsValue = root.value(QLatin1StringView("artists"));
    if (!artistsValue.isArray()) {
        return {};
    }
    const QJsonArray artists = artistsValue.toArray();
    if (artists.size() > MaxLibraryArtistPageSize) {
        return {};
    }

    LibraryArtistPage page;
    page.artists.reserve(artists.size());
    for (const QJsonValue& value : artists) {
        if (!value.isObject()) {
            return {};
        }
        std::optional<LibraryArtist> artist = parseArtist(value.toObject());
        if (!artist) {
            return {};
        }
        page.artists.append(std::move(*artist));
    }
    std::optional<QString> cursor = boundedString(root, QLatin1StringView("nextCursor"), kMaxCursorCharacters);
    if (!cursor) {
        return {};
    }
    page.nextCursor = *cursor;
    if (root.contains(QLatin1StringView("totalCount"))) {
        const std::optional<qint64> total = countValue(root, QLatin1StringView("totalCount"), Presence::Required);
        if (!total) {
            return {};
        }
        page.totalCount = *total;
    }
    page.valid = true;
    return page;
}

std::optional<QList<AccountLibraryTrack>> parseLibraryArtistTracks(const QByteArray& payload)
{
    ParsedAccountLibrary parsed = parseAccountLibrary(payload);
    if (!parsed.valid) {
        return std::nullopt;
    }
    return std::move(parsed.tracks);
}

} // namespace lmp::core::account
