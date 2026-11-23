#pragma once

#include "core/account/LibraryArtistContract.h"
#include "core/config/PrivateConfig.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

// The catalogue as the account service sends it: regions, Home's discovery
// page, charts, shelves and detail pages. Plain values; nothing here is
// stored as it is. A song item carries the Track it plays as.
namespace lmp::core::catalog {

enum class ResourceType {
    Unknown,
    Song,
    Album,
    Playlist,
    Artist,
};

// "song" or "songs" and so on; Unknown otherwise.
[[nodiscard]] ResourceType parseResourceType(QStringView value);
// "song", "album", "playlist", "artist"; empty for Unknown.
[[nodiscard]] QString resourceTypeName(ResourceType type);
// "songs", "albums", "playlists", "artists"; empty for Unknown.
[[nodiscard]] QString resourceTypePlural(ResourceType type);

enum class ChartKind {
    Unknown,
    Global,
    City,
    Genre,
};

[[nodiscard]] ChartKind parseChartKind(QStringView value);
[[nodiscard]] QString chartKindName(ChartKind kind);

// The source words the parsers compare against. They come from the private
// build configuration; in a build without it they are empty and the rules
// that need them are simply inactive. Tests make up their own.
struct SourceTags {
    // Tag of items that carry the catalogue's own numeric ids.
    QString catalog;
    // Tags whose songs and albums the service resolves directly; the first
    // is assumed when an item has none.
    QStringList stream;
    // Lower-case words naming where music comes from.
    QStringList labels;
    // Prefix of an external playlist id.
    QString externalPlaylistPrefix;

    [[nodiscard]] bool isCatalogTag(QStringView tag) const;
    [[nodiscard]] bool isStreamTag(QStringView tag) const;
    // `value`, trimmed and folded, is one of the labels.
    [[nodiscard]] bool isSourceLabel(QStringView value) const;
    // The prefix followed by 10 to 80 of [A-Za-z0-9_-]. False without a prefix.
    [[nodiscard]] bool isExternalPlaylistId(QStringView value) const;

    // The values of the private build configuration.
    [[nodiscard]] static SourceTags fromConfig(const PrivateConfig& config);
};

// A region code: two lower-case letters.
[[nodiscard]] bool isValidRegionCode(QStringView code);
// The trimmed, lower-cased code when it is valid.
[[nodiscard]] std::optional<QString> normalizeRegionCode(QStringView value);

struct Region {
    QString code;
    QString name;
    QString defaultLanguageTag;
};

struct ChartRef {
    QString region;
    ChartKind kind = ChartKind::Unknown;
    QString id;
    ResourceType resourceType = ResourceType::Unknown;

    friend bool operator==(const ChartRef&, const ChartRef&) = default;
};

// A region, a known kind, an id of 1 to 160 of [A-Za-z0-9_.-], a song, album
// or playlist type; city and genre charts list songs only, and a global
// chart's id is "top-<type>s".
[[nodiscard]] bool isValidChartRef(const ChartRef& chart);

struct ChartDescriptor {
    ChartRef ref;
    QString name;
    QString title;
    QString subtitle;
    QString regionName;
    // A cover link or an absolute http(s) address; empty when none.
    QString artworkUrl;
};

struct CityChartGroup {
    QList<ChartDescriptor> charts;
    bool partial = false;
};

struct CityChartGroups {
    CityChartGroup indian;
    CityChartGroup international;
};

enum class ReleaseKind {
    None,
    Single,
    Ep,
};

// The identity an artist card opens its page by, and a catalogue artist id
// to try first when there is one.
struct ArtistPageTarget {
    account::LibraryArtistIdentity identity;
    QString catalogArtistId;
};

struct CatalogItem {
    QString catalogId;
    ResourceType type = ResourceType::Unknown;
    QString title;
    // The artist, or a playlist's curator; never a source label.
    QString subtitle;
    QString artworkUrl;
    QString sourceUrl;
    QString releaseDate;
    QString description;
    QString contentRating;
    QStringList genreNames;
    QString recordLabel;
    // The song as it plays, for songs.
    std::optional<Track> track;
    // Set on artist cards that open by their provider identity.
    std::optional<ArtistPageTarget> artistTarget;
    // Set on album cards that open by their provider identity.
    std::optional<account::LibraryArtistIdentity> collectionTarget;
    // Set on playlist cards that open an external playlist.
    QString externalPlaylistId;
    // Songs on an album, when the catalogue says; 0 otherwise.
    int trackCount = 0;
    ReleaseKind releaseKind = ReleaseKind::None;
};

struct Shelf {
    QString id;
    QString title;
    ResourceType type = ResourceType::Unknown;
    QList<CatalogItem> items;
    // The full chart behind the shelf ("See all").
    std::optional<ChartRef> chart;
    // Where the next page starts, for lists the service pages.
    std::optional<int> nextOffset;
};

struct Chart {
    Shelf shelf;
    ChartDescriptor descriptor;
    bool stale = false;
};

struct Discovery {
    QString region;
    QString regionName;
    QList<Shelf> shelves;
    QList<ChartDescriptor> charts;
    std::optional<CityChartGroups> cityChartGroups;
    std::optional<Shelf> moodShelf;
    std::optional<Shelf> newReleasesShelf;
    std::optional<Shelf> newReleaseSongsShelf;
    std::optional<Shelf> hitsShelf;
    std::optional<Shelf> popularArtistsShelf;
    bool stale = false;
};

// An album, playlist or artist page: the item, its songs and, for artists,
// its sections.
struct Detail {
    CatalogItem item;
    QList<Track> tracks;
    QList<Shelf> sections;
};

struct ItemPage {
    QList<CatalogItem> items;
    std::optional<int> nextOffset;
};

// The sections an artist page can open in full.
enum class ArtistSection {
    TopSongs,
    Albums,
    Singles,
    SimilarArtists,
};

// "top-songs", "albums", "singles", "similar-artists".
[[nodiscard]] QString artistSectionId(ArtistSection section);
[[nodiscard]] std::optional<ArtistSection> parseArtistSectionId(QStringView id);

} // namespace lmp::core::catalog
