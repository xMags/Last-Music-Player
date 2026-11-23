#include "core/catalog/CatalogDetailParser.h"

#include "core/catalog/CatalogParser.h"
#include "core/catalog/CatalogValues.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>

#include <algorithm>

namespace lmp::core::catalog {

namespace values = catalog_values;

namespace {

constexpr qsizetype MaxIdentifierCharacters = 256;
constexpr qsizetype MaxNameCharacters = 256;
constexpr qsizetype MaxUrlCharacters = 4096;

// Control characters other than a line break or a tab.
[[nodiscard]] bool hasControl(const QString& value)
{
    return std::any_of(value.cbegin(), value.cend(), [](QChar character) {
        const char16_t code = character.unicode();
        return (code < 0x20 && code != u'\n' && code != u'\t') || code == 0x7f;
    });
}

// Markup removed and control characters dropped, for text shown as it is.
[[nodiscard]] QString plainText(const QString& value)
{
    QString result;
    result.reserve(value.size());
    bool inTag = false;
    for (const QChar character : value) {
        if (character == QLatin1Char('<')) {
            inTag = true;
            continue;
        }
        if (inTag) {
            inTag = character != QLatin1Char('>');
            continue;
        }
        if (!hasControl(QString(character))) {
            result.append(character);
        }
    }
    return result.trimmed();
}

[[nodiscard]] Result<QJsonObject> rootOf(const QByteArray& payload, const QString& unreadable)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!document.isObject()) {
        return Failure::of(FailureKind::InvalidResponse, unreadable);
    }
    return document.object();
}

[[nodiscard]] bool arrayContains(const QJsonArray& array, QLatin1StringView expected)
{
    return std::any_of(array.cbegin(), array.cend(),
                       [expected](const QJsonValue& value) { return value.isString() && value.toString() == expected; });
}

[[nodiscard]] QList<CatalogItem> catalogItems(const QJsonValue& value, const SourceTags& tags)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : value.toArray()) {
        if (std::optional<CatalogItem> item = parseCatalogItem(entry, tags)) {
            items.append(std::move(*item));
        }
    }
    return items;
}

[[nodiscard]] QList<CatalogItem> releaseItems(const QJsonValue& value, bool singles)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : value.toArray()) {
        if (std::optional<CatalogItem> item = parseProviderReleaseItem(entry, singles)) {
            items.append(std::move(*item));
        }
    }
    return items;
}

[[nodiscard]] QList<CatalogItem> artistItems(const QJsonValue& value)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : value.toArray()) {
        if (std::optional<CatalogItem> item = parseProviderArtistItem(entry)) {
            items.append(std::move(*item));
        }
    }
    return items;
}

void addSection(QList<Shelf>& sections, const QString& id, const QString& title, ResourceType type,
                QList<CatalogItem> items)
{
    if (items.isEmpty()) {
        return;
    }
    Shelf shelf;
    shelf.id = id;
    shelf.title = title;
    shelf.type = type;
    shelf.items = std::move(items);
    sections.append(std::move(shelf));
}

} // namespace

std::optional<QUrlQuery> detailIdentityQuery(const account::LibraryArtistIdentity& identity, const QString& artist)
{
    QUrlQuery query;
    if (!identity.catalogId.isEmpty()) {
        if (identity.catalogId.size() > MaxIdentifierCharacters || hasControl(identity.catalogId)) {
            return std::nullopt;
        }
        query.addQueryItem(QStringLiteral("catalogId"), identity.catalogId);
        return query;
    }
    if (!identity.sourceUrl.isEmpty()) {
        if (identity.sourceUrl.size() > MaxUrlCharacters
            || !net::isSafeRemoteUrl(identity.sourceUrl, net::RemoteUrlUse::Durable)) {
            return std::nullopt;
        }
        query.addQueryItem(QStringLiteral("sourceUrl"), identity.sourceUrl);
        return query;
    }
    if (identity.name.isEmpty() || identity.name.size() > MaxNameCharacters || hasControl(identity.name)) {
        return std::nullopt;
    }
    query.addQueryItem(QStringLiteral("name"), identity.name);
    const QString narrowed = artist.trimmed();
    if (!narrowed.isEmpty() && narrowed.size() <= MaxNameCharacters && !hasControl(narrowed)) {
        query.addQueryItem(QStringLiteral("artist"), narrowed);
    }
    return query;
}

bool providerSupportsCapability(const QByteArray& payload, QLatin1StringView capability)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    if (capability.isEmpty() || root.isEmpty()) {
        return false;
    }
    if (arrayContains(root.value(QLatin1StringView("capabilities")).toArray(), capability)) {
        return true;
    }
    const QJsonArray providers = root.value(QLatin1StringView("providers")).toArray();
    return std::any_of(providers.cbegin(), providers.cend(), [capability](const QJsonValue& provider) {
        return arrayContains(provider.toObject().value(QLatin1StringView("capabilities")).toArray(), capability);
    });
}

Result<Detail> parseCatalogDetail(const QByteArray& payload, ResourceType type, const SourceTags& tags)
{
    const QString key = resourceTypeName(type);
    const Result<QJsonObject> rootResult = rootOf(payload, QStringLiteral("This page could not be loaded. Try again."));
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    std::optional<CatalogItem> item = key.isEmpty() ? std::nullopt : parseCatalogItem(root.value(key), tags);
    if (!item) {
        return Failure::of(FailureKind::NotFound, QStringLiteral("Catalog item is unavailable in this region."));
    }
    Detail detail;
    detail.item = std::move(*item);
    for (const CatalogItem& entry : catalogItems(root.value(QLatin1StringView("tracks")), tags)) {
        if (entry.track) {
            detail.tracks.append(*entry.track);
        }
    }
    if (type != ResourceType::Artist) {
        return detail;
    }
    struct SectionSource {
        const char* id;
        const char* title;
        const char* key;
    };
    static constexpr SectionSource kSections[] = {
        {"latest-release", "Latest Release", "latestRelease"}, {"top-songs", "Top Songs", "topSongs"},
        {"albums", "Albums", "albums"},                        {"singles", "Singles & EPs", "singles"},
        {"similar-artists", "Similar Artists", "similarArtists"},
    };
    for (const SectionSource& source : kSections) {
        QList<CatalogItem> items = catalogItems(root.value(QLatin1StringView(source.key)), tags);
        if (QLatin1StringView(source.id) == QLatin1StringView("singles")) {
            items = asSinglesAndEps(std::move(items));
        }
        if (items.isEmpty()) {
            continue;
        }
        const ResourceType sectionType = items.first().type;
        addSection(detail.sections, QString::fromLatin1(source.id), QString::fromLatin1(source.title), sectionType,
                   std::move(items));
    }
    return detail;
}

Result<Detail> parseProviderArtistDetail(const QByteArray& payload, const account::LibraryArtistIdentity& identity)
{
    const QString unreadable = QStringLiteral("This artist could not be loaded. Try again.");
    const Result<QJsonObject> rootResult = rootOf(payload, unreadable);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    const QJsonObject artist = root.value(QLatin1StringView("artist")).toObject();
    const QString name = values::textOr(artist, "name", identity.name.trimmed());
    if (name.isEmpty()) {
        return Failure::of(FailureKind::InvalidResponse, unreadable);
    }
    Detail detail;
    CatalogItem& item = detail.item;
    item.catalogId = values::textOr(artist, "catalogId", identity.catalogId.isEmpty() ? name : identity.catalogId);
    item.type = ResourceType::Artist;
    item.title = name;
    item.artworkUrl = coverUrl(artist.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = values::textOr(artist, "sourceUrl", identity.sourceUrl);
    item.description = plainText(values::text(artist, "biography"));
    item.genreNames = values::strings(artist.value(QLatin1StringView("genreNames")));

    const QJsonValue releases = root.value(QLatin1StringView("releases"));
    addSection(detail.sections, QStringLiteral("top-songs"), QStringLiteral("Top Songs"), ResourceType::Song,
               parseProviderTrackItems(root.value(QLatin1StringView("topSongs"))));
    addSection(detail.sections, QStringLiteral("albums"), QStringLiteral("Albums"), ResourceType::Album,
               releaseItems(releases, false));
    addSection(detail.sections, QStringLiteral("singles"), QStringLiteral("Singles & EPs"), ResourceType::Album,
               releaseItems(releases, true));
    addSection(detail.sections, QStringLiteral("similar-artists"), QStringLiteral("Similar Artists"),
               ResourceType::Artist, artistItems(root.value(QLatin1StringView("relatedArtists"))));
    return detail;
}

Result<Shelf> parseProviderArtistSection(const QByteArray& payload, ArtistSection section, const QString& title)
{
    const QString unreadable = QStringLiteral("This artist section could not be loaded. Try again.");
    const Result<QJsonObject> rootResult = rootOf(payload, unreadable);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    const QJsonValue entries = root.value(QLatin1StringView("items"));
    Shelf shelf;
    shelf.id = artistSectionId(section);
    shelf.title = title;
    switch (section) {
    case ArtistSection::TopSongs:
        shelf.type = ResourceType::Song;
        shelf.items = parseProviderTrackItems(entries);
        break;
    case ArtistSection::SimilarArtists:
        shelf.type = ResourceType::Artist;
        shelf.items = artistItems(entries);
        break;
    case ArtistSection::Albums:
    case ArtistSection::Singles:
        shelf.type = ResourceType::Album;
        shelf.items = releaseItems(entries, section == ArtistSection::Singles);
        break;
    }
    const ResourceType answered = values::itemType(root);
    if (answered != ResourceType::Unknown && answered != shelf.type) {
        return Failure::of(FailureKind::InvalidResponse, unreadable);
    }
    return shelf;
}

Result<Detail> parseProviderAlbumDetail(const QByteArray& payload, const account::LibraryArtistIdentity& identity)
{
    const QString unreadable = QStringLiteral("This album could not be loaded. Try again.");
    const Result<QJsonObject> rootResult = rootOf(payload, unreadable);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    const QJsonObject release = root.value(QLatin1StringView("release")).toObject();
    const QString title = values::textOr(release, "name", identity.name.trimmed());
    const QString provider = values::textOr(release, "provider", identity.provider.trimmed());
    const QString exactId = values::textOr(release, "catalogId", identity.catalogId.trimmed());
    const QString sourceUrl = values::textOr(release, "sourceUrl", identity.sourceUrl.trimmed());
    const QString catalogId = exactId.isEmpty() ? sourceUrl : exactId;
    if (title.isEmpty() || provider.isEmpty() || catalogId.isEmpty()) {
        return Failure::of(FailureKind::InvalidResponse, unreadable);
    }
    Detail detail;
    CatalogItem& item = detail.item;
    item.catalogId = catalogId;
    item.type = ResourceType::Album;
    item.title = title;
    item.subtitle = values::text(release, "artistName");
    item.artworkUrl = coverUrl(release.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = sourceUrl;
    item.releaseDate = values::text(release, "releaseDate");
    item.collectionTarget = account::LibraryArtistIdentity{exactId, sourceUrl, title, provider};
    detail.tracks = parseProviderTracks(root.value(QLatin1StringView("tracks")));
    return detail;
}

Result<Detail> parseExternalPlaylistDetail(const QByteArray& payload, const QString& id, const SourceTags& tags)
{
    const QString unreadable = QStringLiteral("This playlist could not be loaded. Try again.");
    if (!tags.isExternalPlaylistId(id)) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("Invalid playlist."));
    }
    const Result<QJsonObject> rootResult = rootOf(payload, unreadable);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    QJsonObject playlist = root.value(QLatin1StringView("playlist")).toObject();
    const QString answered = values::text(playlist, "catalogId");
    if (!answered.isEmpty() && answered != id) {
        return Failure::of(FailureKind::InvalidResponse,
                           QStringLiteral("The music catalog returned a different playlist than requested."));
    }
    playlist.insert(QStringLiteral("catalogId"), id);
    if (values::text(playlist, "provider").isEmpty() && !tags.stream.isEmpty()) {
        playlist.insert(QStringLiteral("provider"), tags.stream.first());
    }
    playlist.insert(QStringLiteral("resourceType"), QStringLiteral("playlist"));
    std::optional<CatalogItem> item = parseExternalPlaylistItem(playlist, tags);
    if (!item) {
        return Failure::of(FailureKind::InvalidResponse, unreadable);
    }
    Detail detail;
    detail.item = std::move(*item);
    detail.tracks = parseProviderTracks(root.value(QLatin1StringView("tracks")));
    return detail;
}

} // namespace lmp::core::catalog
