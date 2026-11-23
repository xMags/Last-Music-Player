#pragma once

#include "core/async/Result.h"
#include "core/catalog/CatalogModels.h"

#include <QByteArray>
#include <QJsonValue>
#include <QList>
#include <QString>

#include <optional>

// Readers for the catalogue's answers: regions, Home's discovery page,
// charts and their pages, and the items and shelves inside them. The shape
// is owned by the service, so every reader is defensive: an item it cannot
// use is skipped, a field of the wrong type reads as absent, and a whole
// answer fails only when it is not JSON or names a different region or chart
// than the one asked for. Pure functions, no I/O.
namespace lmp::core::catalog {

// A next page's offset: a whole number of at least 0; anything else means
// the list is complete.
[[nodiscard]] std::optional<int> parseNextOffset(const QJsonValue& value);

// A cover link (/v1/music/art/<id>) or an absolute http(s) address, trimmed;
// empty for anything else.
[[nodiscard]] QString coverUrl(const QJsonValue& value);
// An absolute http(s) address, trimmed; empty for anything else.
[[nodiscard]] QString httpUrl(const QJsonValue& value);

struct ChartRefOptions {
    QString fallbackRegion;
    bool requireExplicitRegion = false;
};

[[nodiscard]] std::optional<ChartRef> parseChartRef(const QJsonValue& value, const ChartRefOptions& options = {});
// A valid reference with a name and a title.
[[nodiscard]] std::optional<ChartDescriptor> parseChartDescriptor(const QJsonValue& value,
                                                                  const ChartRefOptions& options = {});

// A catalogue item with a known type and a catalogue id. The title falls back
// to "name", then "Untitled"; the subtitle is the artist, else the curator,
// never a source label (a playlist's curator loses a leading label). A song
// carries the Track it plays as, when it has a source address that may be
// kept; its genre is folded from its genre names.
[[nodiscard]] std::optional<CatalogItem> parseCatalogItem(const QJsonValue& value, const SourceTags& tags);

// A shelf of catalogue items with a known type and at least one item. Its
// chart comes from "chart", else from a global shelf id ("top-songs"), and is
// dropped when its type differs from the shelf's.
[[nodiscard]] std::optional<Shelf> parseShelf(const QJsonValue& value, const QString& region, const SourceTags& tags);

// Home's extra shelves; nothing when none of their items can be used.
[[nodiscard]] std::optional<Shelf> parseNewReleasesShelf(const QJsonValue& value, const SourceTags& tags);
[[nodiscard]] std::optional<Shelf> parseNewReleaseSongsShelf(const QJsonValue& value, const SourceTags& tags);
[[nodiscard]] std::optional<Shelf> parseHitsShelf(const QJsonValue& value, const SourceTags& tags);
[[nodiscard]] std::optional<Shelf> parsePopularArtistsShelf(const QJsonValue& value, const SourceTags& tags);

// An external playlist as a card, or nothing for another source's item or
// one without a usable id or title.
[[nodiscard]] std::optional<CatalogItem> parseExternalPlaylistItem(const QJsonValue& value, const SourceTags& tags);
// A provider album or single (`singles` picks "single" and "ep" releases,
// marked as such; otherwise "album" releases), opened by its identity.
[[nodiscard]] std::optional<CatalogItem> parseProviderReleaseItem(const QJsonValue& value, bool singles);
// A provider artist card, opened by its identity.
[[nodiscard]] std::optional<CatalogItem> parseProviderArtistItem(const QJsonValue& value);
// Provider songs (the account's track form) that can be kept, in order.
[[nodiscard]] QList<Track> parseProviderTracks(const QJsonValue& value);
// A list of songs from search or related songs: {"results": [...]},
// {"tracks": [...]}, {"result": {...}} or a bare array.
[[nodiscard]] QList<Track> parseProviderTrackList(const QByteArray& payload);
// Provider songs as song cards, in order.
[[nodiscard]] QList<CatalogItem> parseProviderTrackItems(const QJsonValue& value);

// The item marked as a single or an EP: its subtitle starts with "Single"
// or "EP" ("EP · Artist").
[[nodiscard]] CatalogItem withReleaseKind(CatalogItem item, ReleaseKind kind);
// An artist's singles and EPs: an album of one song is a single, a longer
// one an EP; items already marked, or without a song count, stay as they are.
[[nodiscard]] QList<CatalogItem> asSinglesAndEps(QList<CatalogItem> items);

// {"storefronts": [{code, name, defaultLanguageTag}]}: valid codes only; the
// name falls back to the upper-case code.
[[nodiscard]] QList<Region> parseRegions(const QByteArray& payload);

// The discovery answer for `region`. Fails when it is not JSON or names
// another region. The region's name comes from `regions`, else the upper-case
// code.
[[nodiscard]] Result<Discovery> parseDiscovery(const QByteArray& payload, const QString& region,
                                               const QList<Region>& regions, const SourceTags& tags);

// A chart's first page for `requested`. A global chart without a descriptor
// uses `known` (the one discovery holds), else "Top Songs" and the like with
// the subtitle "Your music region". Fails when it is not JSON, names another
// region, or describes another chart.
[[nodiscard]] Result<Chart> parseChart(const QByteArray& payload, const ChartRef& requested,
                                       const std::optional<ChartDescriptor>& known, const SourceTags& tags);

// A later page of a chart. Fails as parseChart does.
[[nodiscard]] Result<ItemPage> parseChartPage(const QByteArray& payload, const ChartRef& requested,
                                              const SourceTags& tags);

// What a search found besides its songs: the top result (one of its songs,
// by index, or an artist, album or playlist card) and the artist, album and
// playlist cards (/v1/music/search/all).
struct SearchAll {
    QList<Track> songs;
    // "song", "artist", "album", "playlist"; empty without a top result.
    QString topKind;
    int topSongIndex = -1;
    std::optional<CatalogItem> topItem;
    QList<CatalogItem> artists;
    QList<CatalogItem> albums;
    QList<CatalogItem> playlists;
};

// Items the app cannot open are dropped; a top result that is not one of
// them, or a song not among the songs, becomes none. Artist cards say
// "Artist"; a single or EP says so.
[[nodiscard]] SearchAll parseSearchAll(const QByteArray& payload, const SourceTags& tags);

} // namespace lmp::core::catalog
