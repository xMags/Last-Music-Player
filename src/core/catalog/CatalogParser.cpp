#include "core/catalog/CatalogParser.h"

#include "core/account/AccountLibrarySync.h"
#include "core/account/AccountMusicContract.h"
#include "core/catalog/CatalogValues.h"
#include "core/catalog/MusicGenres.h"
#include "core/net/ArtworkUrl.h"
#include "core/text/CaseFold.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1StringView>

#include <algorithm>
#include <cmath>

namespace lmp::core::catalog {

namespace values = catalog_values;

namespace {

// A catalogue id for a song or album of the catalogue's own: digits only.
[[nodiscard]] bool isCatalogNumericId(const QString& id)
{
    return !id.isEmpty() && id.size() <= 20
        && std::all_of(id.cbegin(), id.cend(),
                       [](QChar character) { return character >= QLatin1Char('0') && character <= QLatin1Char('9'); });
}

// The playlist curator without a leading source label ("<label> Fitness"
// is "Fitness"); other text as it is.
[[nodiscard]] QString withoutLeadingLabel(const QString& value, const SourceTags& tags)
{
    const QString folded = foldCaseAscii(value);
    for (const QString& label : tags.labels) {
        const QString prefix = foldCaseAscii(label.trimmed());
        if (prefix.isEmpty() || !folded.startsWith(prefix) || folded.size() <= prefix.size()
            || !folded.at(prefix.size()).isSpace()) {
            continue;
        }
        const QString rest = value.mid(prefix.size()).trimmed();
        if (!rest.isEmpty()) {
            return rest;
        }
    }
    return value;
}

// The song a catalogue item plays as, in the account's track form.
[[nodiscard]] std::optional<Track> catalogSong(const QJsonObject& raw, const CatalogItem& item, const SourceTags& tags)
{
    const QString provider = values::text(raw, "provider").isEmpty() ? tags.catalog : values::text(raw, "provider");
    QString id = values::text(raw, "id");
    if (id.isEmpty()) {
        id = tags.catalog.isEmpty() ? item.catalogId : tags.catalog + QLatin1Char(':') + item.catalogId;
    }
    const double durationMs = raw.value(QLatin1StringView("durationMs")).toDouble();
    const QJsonObject portable{
        {QStringLiteral("id"), id},
        {QStringLiteral("provider"), provider},
        {QStringLiteral("title"), item.title},
        {QStringLiteral("artist"), item.subtitle.isEmpty() ? QStringLiteral("Unknown artist") : item.subtitle},
        {QStringLiteral("album"), values::text(raw, "albumName")},
        {QStringLiteral("durationMs"), std::isfinite(durationMs) && durationMs > 0 ? durationMs : 0.0},
        {QStringLiteral("sourceUrl"), item.sourceUrl},
        {QStringLiteral("artworkUrl"), item.artworkUrl},
        {QStringLiteral("catalogId"), item.catalogId},
        {QStringLiteral("genre"), canonicalGenre(item.genreNames)},
        {QStringLiteral("albumCatalogId"), values::text(raw, "albumCatalogId")},
        {QStringLiteral("artistCatalogId"), values::text(raw, "artistCatalogId")},
        {QStringLiteral("sourceKind"), QStringLiteral("remote")},
    };
    const std::optional<account::PortableAccountTrack> track =
        account::parsePortableTrack(QJsonDocument(portable).toJson(QJsonDocument::Compact));
    if (!track) {
        return std::nullopt;
    }
    return account::trackFromPortable(*track);
}

// A provider song as a song card.
[[nodiscard]] CatalogItem songCard(const Track& track)
{
    CatalogItem item;
    item.catalogId = track.catalogId.isEmpty() ? track.remoteId : track.catalogId;
    item.type = ResourceType::Song;
    item.title = track.title;
    item.subtitle = track.artist;
    item.artworkUrl = track.artworkUrl;
    item.sourceUrl = track.sourceUrl;
    if (!track.genre.isEmpty()) {
        item.genreNames.append(track.genre);
    }
    item.track = track;
    return item;
}

// One provider song (the account's track form), when it can be kept.
[[nodiscard]] std::optional<Track> providerTrack(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const std::optional<account::PortableAccountTrack> track =
        account::parsePortableTrack(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    if (!track) {
        return std::nullopt;
    }
    return account::trackFromPortable(*track);
}

[[nodiscard]] account::LibraryArtistIdentity identityOf(const QJsonObject& raw, const QString& name,
                                                        const QString& provider)
{
    return account::LibraryArtistIdentity{values::text(raw, "catalogId"), values::text(raw, "sourceUrl"), name,
                                          provider};
}

[[nodiscard]] std::optional<CatalogItem> newRelease(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    const QString catalogId = values::text(raw, "catalogId");
    const QString provider = values::text(raw, "provider");
    const QString title = values::text(raw, "title");
    const bool catalogOwn = tags.isCatalogTag(provider);
    if (catalogId.isEmpty() || title.isEmpty() || (!tags.isStreamTag(provider) && !catalogOwn)) {
        return std::nullopt;
    }
    if (catalogOwn
        && (!isCatalogNumericId(catalogId) || raw.value(QLatin1StringView("resourceType")) != QLatin1StringView("album"))) {
        return std::nullopt;
    }
    const QString artist = values::text(raw, "artistName");
    const QString releaseType = foldCaseAscii(values::text(raw, "releaseType"));
    const QString kind = releaseType == QLatin1StringView("single") ? QStringLiteral("Single")
        : releaseType == QLatin1StringView("ep")                    ? QStringLiteral("EP")
                                                                    : QStringLiteral("Album");
    CatalogItem item;
    item.catalogId = catalogId;
    item.type = ResourceType::Album;
    item.title = title;
    item.subtitle = artist.isEmpty() ? kind : kind + QStringLiteral(" · ") + artist;
    item.artworkUrl = coverUrl(raw.value(QLatin1StringView("artworkUrl")));
    const QString sourceUrl = values::text(raw, "sourceUrl");
    if (catalogOwn) {
        // Opens the catalogue's album page, as chart albums do.
        item.sourceUrl = httpUrl(sourceUrl);
        item.releaseDate = values::text(raw, "releaseDate");
        item.contentRating = values::text(raw, "contentRating");
        item.genreNames = values::strings(raw.value(QLatin1StringView("genreNames")));
        item.recordLabel = values::text(raw, "recordLabel");
        return item;
    }
    item.sourceUrl = sourceUrl;
    item.collectionTarget = account::LibraryArtistIdentity{catalogId, sourceUrl, title, provider};
    return item;
}

[[nodiscard]] std::optional<CatalogItem> newReleaseSong(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    if (tags.isCatalogTag(values::text(raw, "provider"))) {
        if (raw.value(QLatin1StringView("resourceType")) != QLatin1StringView("song")
            || !isCatalogNumericId(values::text(raw, "catalogId")) || values::text(raw, "title").isEmpty()) {
            return std::nullopt;
        }
        std::optional<CatalogItem> song = parseCatalogItem(raw, tags);
        if (!song || !song->track || !tags.isCatalogTag(song->track->sourceTag)) {
            return std::nullopt;
        }
        return song;
    }
    const std::optional<Track> track = providerTrack(raw.value(QLatin1StringView("track")));
    if (!track || !tags.isStreamTag(track->sourceTag)) {
        return std::nullopt;
    }
    CatalogItem item = songCard(*track);
    item.genreNames.clear();
    return item;
}

} // namespace

std::optional<int> parseNextOffset(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number != std::floor(number) || number > values::MaxSafeOffset) {
        return std::nullopt;
    }
    return static_cast<int>(number);
}

QString httpUrl(const QJsonValue& value)
{
    const QString address = value.isString() ? value.toString().trimmed() : QString();
    const QString folded = foldCaseAscii(address.left(8));
    return folded.startsWith(QLatin1StringView("https://")) || folded.startsWith(QLatin1StringView("http://"))
        ? address
        : QString();
}

QString coverUrl(const QJsonValue& value)
{
    const QString address = value.isString() ? value.toString().trimmed() : QString();
    const QString link = net::accountCoverLinkPath(address);
    return link.isEmpty() ? httpUrl(address) : link;
}

std::optional<ChartRef> parseChartRef(const QJsonValue& value, const ChartRefOptions& options)
{
    const QJsonObject raw = value.toObject();
    const std::optional<QString> explicitRegion = normalizeRegionCode(values::text(raw, "storefront"));
    if (options.requireExplicitRegion && !explicitRegion) {
        return std::nullopt;
    }
    const std::optional<QString> region = explicitRegion ? explicitRegion : normalizeRegionCode(options.fallbackRegion);
    if (!region) {
        return std::nullopt;
    }
    const ChartRef chart{*region, parseChartKind(values::text(raw, "kind")), values::text(raw, "id"),
                         values::itemType(raw)};
    return isValidChartRef(chart) ? std::optional<ChartRef>(chart) : std::nullopt;
}

std::optional<ChartDescriptor> parseChartDescriptor(const QJsonValue& value, const ChartRefOptions& options)
{
    const std::optional<ChartRef> ref = parseChartRef(value, options);
    if (!ref) {
        return std::nullopt;
    }
    const QJsonObject raw = value.toObject();
    ChartDescriptor descriptor{*ref,
                               values::text(raw, "name"),
                               values::text(raw, "title"),
                               values::text(raw, "subtitle"),
                               values::text(raw, "storefrontName"),
                               coverUrl(raw.value(QLatin1StringView("artworkUrl")))};
    if (descriptor.name.isEmpty() || descriptor.title.isEmpty()) {
        return std::nullopt;
    }
    return descriptor;
}

std::optional<CatalogItem> parseCatalogItem(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    const ResourceType type = values::itemType(raw);
    const QString catalogId = values::text(raw, "catalogId");
    if (type == ResourceType::Unknown || catalogId.isEmpty()) {
        return std::nullopt;
    }
    CatalogItem item;
    item.catalogId = catalogId;
    item.type = type;
    item.title = values::text(raw, "title");
    if (item.title.isEmpty()) {
        item.title = values::text(raw, "name");
    }
    if (item.title.isEmpty()) {
        item.title = QStringLiteral("Untitled");
    }
    // A playlist's curator is often copied into the artist field, so either
    // can name a source; that is never shown as a byline.
    const auto named = [&tags, type](const QString& text) {
        const QString name = type == ResourceType::Playlist ? withoutLeadingLabel(text, tags) : text;
        return tags.isSourceLabel(name) ? QString() : name;
    };
    item.subtitle = named(values::text(raw, "artistName"));
    if (item.subtitle.isEmpty()) {
        item.subtitle = named(values::text(raw, "curatorName"));
    }
    item.artworkUrl = coverUrl(raw.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = values::text(raw, "sourceUrl");
    item.genreNames = values::strings(raw.value(QLatin1StringView("genreNames")));
    const QJsonObject notes = raw.value(QLatin1StringView("editorialNotes")).toObject();
    // Notes are an object of standard, short and tagline text, or plain text.
    for (const QString& text : {values::text(raw, "description"), values::text(notes, "standard"),
                                values::text(notes, "short"), values::text(notes, "tagline"),
                                values::text(raw, "editorialNotes")}) {
        if (!text.isEmpty()) {
            item.description = text;
            break;
        }
    }
    item.releaseDate = values::text(raw, "releaseDate");
    item.contentRating = values::text(raw, "contentRating");
    item.recordLabel = values::text(raw, "recordLabel");
    if (type == ResourceType::Album) {
        item.trackCount = values::nonNegativeInteger(raw.value(QLatin1StringView("trackCount")));
    }
    if (type == ResourceType::Song) {
        item.track = catalogSong(raw, item, tags);
    }
    return item;
}

std::optional<Shelf> parseShelf(const QJsonValue& value, const QString& region, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    Shelf shelf;
    shelf.type = values::itemType(raw);
    for (const QJsonValue& entry : raw.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = parseCatalogItem(entry, tags)) {
            shelf.items.append(std::move(*item));
        }
    }
    if (shelf.type == ResourceType::Unknown || shelf.items.isEmpty()) {
        return std::nullopt;
    }
    shelf.id = values::text(raw, "id");
    shelf.title = values::text(raw, "title");
    shelf.chart = parseChartRef(raw.value(QLatin1StringView("chart")), {.fallbackRegion = region});
    if (!shelf.chart) {
        // Older answers name a global chart by the shelf id alone.
        const ChartRef legacy{region, ChartKind::Global, shelf.id, shelf.type};
        if (shelf.id == QStringLiteral("top-") + resourceTypePlural(shelf.type) && isValidChartRef(legacy)) {
            shelf.chart = legacy;
        }
    }
    if (shelf.chart && shelf.chart->resourceType != shelf.type) {
        shelf.chart.reset();
    }
    return shelf;
}

std::optional<Shelf> parseNewReleasesShelf(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    Shelf shelf;
    for (const QJsonValue& entry : raw.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = newRelease(entry, tags)) {
            shelf.items.append(std::move(*item));
        }
    }
    if (shelf.items.isEmpty()) {
        return std::nullopt;
    }
    shelf.id = values::textOr(raw, "id", QStringLiteral("new-releases"));
    shelf.title = values::textOr(raw, "title", QStringLiteral("New releases"));
    shelf.type = ResourceType::Album;
    return shelf;
}

std::optional<Shelf> parseNewReleaseSongsShelf(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    Shelf shelf;
    for (const QJsonValue& entry : raw.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = newReleaseSong(entry, tags)) {
            shelf.items.append(std::move(*item));
        }
    }
    if (shelf.items.isEmpty()) {
        return std::nullopt;
    }
    shelf.id = values::textOr(raw, "id", QStringLiteral("new-release-songs"));
    shelf.title = values::textOr(raw, "title", QStringLiteral("New releases"));
    shelf.type = ResourceType::Song;
    return shelf;
}

std::optional<CatalogItem> parseExternalPlaylistItem(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    const QString catalogId = values::text(raw, "catalogId");
    const QString title = values::text(raw, "title");
    if (!tags.isStreamTag(values::text(raw, "provider")) || !tags.isExternalPlaylistId(catalogId) || title.isEmpty()) {
        return std::nullopt;
    }
    if (raw.contains(QLatin1StringView("resourceType"))
        && raw.value(QLatin1StringView("resourceType")) != QLatin1StringView("playlist")) {
        return std::nullopt;
    }
    CatalogItem item;
    item.catalogId = catalogId;
    item.type = ResourceType::Playlist;
    item.title = title;
    // Who made it, never where the music comes from.
    const QString subtitle = values::text(raw, "subtitle");
    item.subtitle = tags.isSourceLabel(subtitle) ? QString() : subtitle;
    item.artworkUrl = coverUrl(raw.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = httpUrl(raw.value(QLatin1StringView("sourceUrl")));
    item.description = values::text(raw, "description");
    item.externalPlaylistId = catalogId;
    return item;
}

std::optional<Shelf> parseHitsShelf(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    Shelf shelf;
    for (const QJsonValue& entry : raw.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = parseExternalPlaylistItem(entry, tags)) {
            shelf.items.append(std::move(*item));
        }
    }
    if (shelf.items.isEmpty()) {
        return std::nullopt;
    }
    shelf.id = values::textOr(raw, "id", QStringLiteral("biggest-hits"));
    shelf.title = values::textOr(raw, "title", QStringLiteral("Biggest hits"));
    shelf.type = ResourceType::Playlist;
    return shelf;
}

std::optional<Shelf> parsePopularArtistsShelf(const QJsonValue& value, const SourceTags& tags)
{
    Q_UNUSED(tags);
    const QJsonObject raw = value.toObject();
    Shelf shelf;
    for (const QJsonValue& entry : raw.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = parseProviderArtistItem(entry)) {
            shelf.items.append(std::move(*item));
        }
    }
    if (shelf.items.isEmpty()) {
        return std::nullopt;
    }
    shelf.id = values::textOr(raw, "id", QStringLiteral("popular-artists"));
    shelf.title = values::textOr(raw, "title", QStringLiteral("Popular artists"));
    shelf.type = ResourceType::Artist;
    return shelf;
}

std::optional<CatalogItem> parseProviderReleaseItem(const QJsonValue& value, bool singles)
{
    const QJsonObject raw = value.toObject();
    const QString releaseType = foldCaseAscii(values::text(raw, "releaseType"));
    const bool wanted = singles
        ? (releaseType == QLatin1StringView("single") || releaseType == QLatin1StringView("ep"))
        : releaseType == QLatin1StringView("album");
    const QString sourceUrl = values::text(raw, "sourceUrl");
    const QString catalogId = values::textOr(raw, "catalogId", sourceUrl);
    const QString title = values::text(raw, "name");
    const QString provider = values::text(raw, "provider");
    if (!wanted || catalogId.isEmpty() || title.isEmpty() || provider.isEmpty()) {
        return std::nullopt;
    }
    CatalogItem item;
    item.catalogId = catalogId;
    item.type = ResourceType::Album;
    item.title = title;
    item.subtitle = values::text(raw, "artistName");
    item.artworkUrl = coverUrl(raw.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = sourceUrl;
    item.releaseDate = values::text(raw, "releaseDate");
    item.collectionTarget = identityOf(raw, title, provider);
    if (!singles) {
        return item;
    }
    return withReleaseKind(std::move(item),
                           releaseType == QLatin1StringView("ep") ? ReleaseKind::Ep : ReleaseKind::Single);
}

std::optional<CatalogItem> parseProviderArtistItem(const QJsonValue& value)
{
    const QJsonObject raw = value.toObject();
    const QString sourceUrl = values::text(raw, "sourceUrl");
    const QString catalogId = values::textOr(raw, "catalogId", sourceUrl);
    const QString name = values::text(raw, "name");
    const QString provider = values::text(raw, "provider");
    if (catalogId.isEmpty() || name.isEmpty() || provider.isEmpty()) {
        return std::nullopt;
    }
    CatalogItem item;
    item.catalogId = catalogId;
    item.type = ResourceType::Artist;
    item.title = name;
    item.artworkUrl = coverUrl(raw.value(QLatin1StringView("artworkUrl")));
    item.sourceUrl = sourceUrl;
    item.artistTarget = ArtistPageTarget{identityOf(raw, name, provider), {}};
    return item;
}

QList<Track> parseProviderTracks(const QJsonValue& value)
{
    QList<Track> tracks;
    for (const QJsonValue& entry : value.toArray()) {
        if (std::optional<Track> track = providerTrack(entry)) {
            tracks.append(std::move(*track));
        }
    }
    return tracks;
}

QList<CatalogItem> parseProviderTrackItems(const QJsonValue& value)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : value.toArray()) {
        if (const std::optional<Track> track = providerTrack(entry)) {
            items.append(songCard(*track));
        }
    }
    return items;
}

CatalogItem withReleaseKind(CatalogItem item, ReleaseKind kind)
{
    if (kind == ReleaseKind::None) {
        return item;
    }
    const QString label = kind == ReleaseKind::Single ? QStringLiteral("Single") : QStringLiteral("EP");
    item.releaseKind = kind;
    item.subtitle = item.subtitle.isEmpty() ? label : label + QStringLiteral(" · ") + item.subtitle;
    return item;
}

QList<CatalogItem> asSinglesAndEps(QList<CatalogItem> items)
{
    for (CatalogItem& item : items) {
        if (item.type != ResourceType::Album || item.releaseKind != ReleaseKind::None || item.trackCount <= 0) {
            continue;
        }
        item = withReleaseKind(std::move(item), item.trackCount == 1 ? ReleaseKind::Single : ReleaseKind::Ep);
    }
    return items;
}

namespace {

[[nodiscard]] std::optional<CatalogItem> searchArtist(const QJsonValue& value)
{
    std::optional<CatalogItem> item = parseProviderArtistItem(value);
    if (item) {
        item->subtitle = QStringLiteral("Artist");
    }
    return item;
}

// The catalogue's own album or editorial playlist from search: a numeric
// album id, or a playlist id "pl." and 1 to 80 word characters or dashes.
[[nodiscard]] std::optional<CatalogItem> searchCatalogueItem(const QJsonObject& raw, ResourceType type,
                                                             const SourceTags& tags)
{
    const QString id = values::text(raw, "catalogId");
    bool idOk = false;
    if (type == ResourceType::Album) {
        idOk = isCatalogNumericId(id);
    } else if (id.startsWith(QLatin1StringView("pl.")) && id.size() > 3 && id.size() <= 83) {
        idOk = std::all_of(id.cbegin() + 3, id.cend(), [](QChar character) {
            return character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-');
        });
    }
    if (values::itemType(raw) != type || !idOk) {
        return std::nullopt;
    }
    std::optional<CatalogItem> item = parseCatalogItem(raw, tags);
    if (!item) {
        return std::nullopt;
    }
    const QString releaseType = foldCaseAscii(values::text(raw, "releaseType"));
    if (type == ResourceType::Album
        && (releaseType == QLatin1StringView("single") || releaseType == QLatin1StringView("ep"))) {
        return withReleaseKind(std::move(*item),
                               releaseType == QLatin1StringView("ep") ? ReleaseKind::Ep : ReleaseKind::Single);
    }
    return item;
}

[[nodiscard]] std::optional<CatalogItem> searchAlbum(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    if (tags.isCatalogTag(values::text(raw, "provider"))) {
        return searchCatalogueItem(raw, ResourceType::Album, tags);
    }
    const QString releaseType = foldCaseAscii(values::text(raw, "releaseType"));
    return parseProviderReleaseItem(raw, releaseType == QLatin1StringView("single")
                                             || releaseType == QLatin1StringView("ep"));
}

[[nodiscard]] std::optional<CatalogItem> searchPlaylist(const QJsonValue& value, const SourceTags& tags)
{
    const QJsonObject raw = value.toObject();
    if (tags.isCatalogTag(values::text(raw, "provider"))) {
        return searchCatalogueItem(raw, ResourceType::Playlist, tags);
    }
    return parseExternalPlaylistItem(raw, tags);
}

template <typename Read>
[[nodiscard]] QList<CatalogItem> collectItems(const QJsonValue& list, Read read)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : list.toArray()) {
        if (std::optional<CatalogItem> item = read(entry)) {
            items.append(std::move(*item));
        }
    }
    return items;
}

} // namespace

SearchAll parseSearchAll(const QByteArray& payload, const SourceTags& tags)
{
    const QJsonObject root = QJsonDocument::fromJson(payload).object();
    SearchAll result;
    result.songs = parseProviderTracks(root.value(QLatin1StringView("songs")));
    result.artists = collectItems(root.value(QLatin1StringView("artists")),
                                  [](const QJsonValue& entry) { return searchArtist(entry); });
    result.albums = collectItems(root.value(QLatin1StringView("albums")),
                                 [&tags](const QJsonValue& entry) { return searchAlbum(entry, tags); });
    result.playlists = collectItems(root.value(QLatin1StringView("playlists")),
                                    [&tags](const QJsonValue& entry) { return searchPlaylist(entry, tags); });

    const QJsonObject top = root.value(QLatin1StringView("top")).toObject();
    const QString kind = values::text(top, "kind");
    if (kind == QLatin1StringView("song")) {
        // Matched to the listed song, so the top card plays and likes it.
        const QList<Track> one = parseProviderTracks(QJsonArray{top.value(QLatin1StringView("item"))});
        for (qsizetype index = 0; !one.isEmpty() && index < result.songs.size(); ++index) {
            if (result.songs.at(index).remoteId == one.first().remoteId) {
                result.topKind = kind;
                result.topSongIndex = static_cast<int>(index);
                break;
            }
        }
        return result;
    }
    std::optional<CatalogItem> item;
    const QJsonValue topItem = top.value(QLatin1StringView("item"));
    if (kind == QLatin1StringView("artist")) {
        item = searchArtist(topItem);
    } else if (kind == QLatin1StringView("album")) {
        item = searchAlbum(topItem, tags);
    } else if (kind == QLatin1StringView("playlist")) {
        item = searchPlaylist(topItem, tags);
    }
    if (item) {
        result.topKind = kind;
        result.topItem = std::move(item);
    }
    return result;
}

QList<Region> parseRegions(const QByteArray& payload)
{
    QList<Region> regions;
    for (const QJsonValue& entry :
         QJsonDocument::fromJson(payload).object().value(QLatin1StringView("storefronts")).toArray()) {
        const QJsonObject raw = entry.toObject();
        const std::optional<QString> code = normalizeRegionCode(values::text(raw, "code"));
        if (!code) {
            continue;
        }
        regions.append(Region{*code, values::textOr(raw, "name", code->toUpper()), values::text(raw, "defaultLanguageTag")});
    }
    return regions;
}

Result<Discovery> parseDiscovery(const QByteArray& payload, const QString& region, const QList<Region>& regions,
                                 const SourceTags& tags)
{
    const std::optional<QString> requested = normalizeRegionCode(region);
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!requested || !document.isObject()) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("The music catalog could not be read."));
    }
    const QJsonObject root = document.object();
    const std::optional<QString> answered = normalizeRegionCode(values::text(root, "storefront"));
    if (answered && *answered != *requested) {
        return Failure::of(FailureKind::InvalidResponse,
                           QStringLiteral("The music catalog returned a different region than requested."));
    }
    Discovery discovery;
    discovery.region = *requested;
    const auto named = std::find_if(regions.cbegin(), regions.cend(),
                                    [&discovery](const Region& entry) { return entry.code == discovery.region; });
    discovery.regionName = named != regions.cend() ? named->name : discovery.region.toUpper();
    for (const QJsonValue& entry : root.value(QLatin1StringView("shelves")).toArray()) {
        if (std::optional<Shelf> shelf = parseShelf(entry, discovery.region, tags)) {
            discovery.shelves.append(std::move(*shelf));
        }
    }
    for (const QJsonValue& entry : root.value(QLatin1StringView("charts")).toArray()) {
        if (std::optional<ChartDescriptor> chart = parseChartDescriptor(entry, {.fallbackRegion = discovery.region})) {
            discovery.charts.append(std::move(*chart));
        }
    }
    const QJsonObject groups = root.value(QLatin1StringView("cityChartGroups")).toObject();
    if (!groups.isEmpty()) {
        const auto group = [](const QJsonValue& value) {
            CityChartGroup result;
            const QJsonObject raw = value.toObject();
            for (const QJsonValue& entry : raw.value(QLatin1StringView("charts")).toArray()) {
                std::optional<ChartDescriptor> chart = parseChartDescriptor(entry, {.requireExplicitRegion = true});
                if (chart && chart->ref.kind == ChartKind::City) {
                    result.charts.append(std::move(*chart));
                }
            }
            result.partial = raw.value(QLatin1StringView("partial")).toBool(false);
            return result;
        };
        discovery.cityChartGroups = CityChartGroups{group(groups.value(QLatin1StringView("indian"))),
                                                    group(groups.value(QLatin1StringView("international")))};
    }
    std::optional<Shelf> mood = parseShelf(root.value(QLatin1StringView("moodActivityShelf")), discovery.region, tags);
    if (mood && mood->type == ResourceType::Playlist) {
        discovery.moodShelf = std::move(mood);
    }
    discovery.newReleasesShelf = parseNewReleasesShelf(root.value(QLatin1StringView("newReleasesShelf")), tags);
    discovery.newReleaseSongsShelf =
        parseNewReleaseSongsShelf(root.value(QLatin1StringView("newReleaseSongsShelf")), tags);
    discovery.hitsShelf = parseHitsShelf(root.value(QLatin1StringView("hitsShelf")), tags);
    discovery.popularArtistsShelf =
        parsePopularArtistsShelf(root.value(QLatin1StringView("popularArtistsShelf")), tags);
    discovery.stale = root.value(QLatin1StringView("stale")).toBool(false);
    return discovery;
}

namespace {

// The checks shared by a chart's first and later pages: JSON, the region,
// and a descriptor, when present, for the same chart.
[[nodiscard]] Result<QJsonObject> chartRoot(const QByteArray& payload, const ChartRef& requested)
{
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    if (!isValidChartRef(requested) || !document.isObject()) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("The music catalog could not be read."));
    }
    const QJsonObject root = document.object();
    const QString answeredRegion = values::text(root, "storefront");
    if (!answeredRegion.isEmpty() && normalizeRegionCode(answeredRegion) != requested.region) {
        return Failure::of(FailureKind::InvalidResponse,
                           QStringLiteral("The music catalog returned a chart for a different region."));
    }
    // The answer names its chart by the bare id or by "<kind>:<id>", and its
    // type in the plural; either must be the chart asked for.
    const QString answeredId = values::text(root, "id");
    const QString answeredType = values::text(root, "type");
    if ((!answeredId.isEmpty() && answeredId != requested.id
         && answeredId != chartKindName(requested.kind) + QLatin1Char(':') + requested.id)
        || (!answeredType.isEmpty() && parseResourceType(answeredType) != requested.resourceType)) {
        return Failure::of(FailureKind::InvalidResponse,
                           QStringLiteral("The music catalog returned a different chart than requested."));
    }
    return root;
}

[[nodiscard]] Failure otherChart()
{
    return Failure::of(FailureKind::InvalidResponse,
                       QStringLiteral("The music catalog returned a different chart than requested."));
}

[[nodiscard]] QList<CatalogItem> chartItems(const QJsonObject& root, const SourceTags& tags)
{
    QList<CatalogItem> items;
    for (const QJsonValue& entry : root.value(QLatin1StringView("items")).toArray()) {
        if (std::optional<CatalogItem> item = parseCatalogItem(entry, tags)) {
            items.append(std::move(*item));
        }
    }
    return items;
}

} // namespace

Result<Chart> parseChart(const QByteArray& payload, const ChartRef& requested,
                         const std::optional<ChartDescriptor>& known, const SourceTags& tags)
{
    const Result<QJsonObject> rootResult = chartRoot(payload, requested);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    std::optional<ChartDescriptor> descriptor =
        parseChartDescriptor(root.value(QLatin1StringView("descriptor")), {.fallbackRegion = requested.region});
    if (requested.kind == ChartKind::Global && !descriptor) {
        // The card that opened it named the chart; use what discovery holds
        // before a generic "Top Songs".
        descriptor = known;
    }
    if (requested.kind == ChartKind::Global && !descriptor) {
        const QString plural = resourceTypePlural(requested.resourceType);
        const QString title = values::textOr(root, "title", QStringLiteral("Top ") + plural.left(1).toUpper() + plural.mid(1));
        descriptor = ChartDescriptor{requested, title, title, QStringLiteral("Your music region"), {}, {}};
    }
    if (!descriptor || descriptor->ref != requested) {
        return otherChart();
    }
    Chart chart;
    chart.shelf.id = values::textOr(root, "id", chartKindName(requested.kind) + QLatin1Char(':') + requested.id);
    chart.shelf.title = values::textOr(root, "title", descriptor->title);
    chart.shelf.type = requested.resourceType;
    chart.shelf.items = chartItems(root, tags);
    chart.shelf.nextOffset = parseNextOffset(root.value(QLatin1StringView("nextOffset")));
    chart.descriptor = std::move(*descriptor);
    chart.stale = root.value(QLatin1StringView("stale")).toBool(false);
    return chart;
}

Result<ItemPage> parseChartPage(const QByteArray& payload, const ChartRef& requested, const SourceTags& tags)
{
    const Result<QJsonObject> rootResult = chartRoot(payload, requested);
    if (!rootResult) {
        return rootResult.failure();
    }
    const QJsonObject& root = rootResult.value();
    const std::optional<ChartDescriptor> descriptor =
        parseChartDescriptor(root.value(QLatin1StringView("descriptor")), {.fallbackRegion = requested.region});
    if (descriptor
        && (descriptor->ref.kind != requested.kind || descriptor->ref.id != requested.id
            || descriptor->ref.resourceType != requested.resourceType)) {
        return otherChart();
    }
    return ItemPage{chartItems(root, tags), parseNextOffset(root.value(QLatin1StringView("nextOffset")))};
}

} // namespace lmp::core::catalog
