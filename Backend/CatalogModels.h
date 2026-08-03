#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // Shapes for the catalog discovery surface. These mirror the service payload
    // rather than the local database: nothing here is persisted, and an item's
    // SourceUrl is what makes it playable, through the same resolve path every
    // other remote track uses.

    enum class CatalogResourceType
    {
        Unknown,
        Song,
        Album,
        Playlist,
        Artist,
    };

    CatalogResourceType ParseCatalogResourceType(winrt::hstring const& value) noexcept;
    winrt::hstring CatalogResourceTypeName(CatalogResourceType type) noexcept;

    enum class CatalogChartKind
    {
        Unknown,
        Global,
        City,
        Genre,
    };

    CatalogChartKind ParseCatalogChartKind(winrt::hstring const& value) noexcept;
    winrt::hstring CatalogChartKindName(CatalogChartKind kind) noexcept;

    struct CatalogChartRef
    {
        winrt::hstring Storefront;
        CatalogChartKind Kind{ CatalogChartKind::Unknown };
        winrt::hstring Id;
        CatalogResourceType ResourceType{ CatalogResourceType::Unknown };
    };

    bool IsValidCatalogStorefront(winrt::hstring const& storefront) noexcept;
    bool IsValidCatalogChartRef(CatalogChartRef const& chart) noexcept;

    struct CatalogChartDescriptor
    {
        CatalogChartRef Ref;
        winrt::hstring Name;
        winrt::hstring Title;
        winrt::hstring Subtitle;
        winrt::hstring StorefrontName;
        winrt::hstring ArtworkUrl;
    };

    struct CatalogCityChartGroup
    {
        std::vector<CatalogChartDescriptor> Charts;
        bool Partial{ false };
    };

    struct CatalogCityChartGroups
    {
        CatalogCityChartGroup Indian;
        CatalogCityChartGroup International;
    };

    struct CatalogItem
    {
        CatalogResourceType Type{ CatalogResourceType::Unknown };
        // Stable account-library identity, for example `apple-music:1811023667`.
        // CatalogId remains the provider's resource id used by detail routes.
        winrt::hstring RemoteId;
        winrt::hstring CatalogId;
        // Songs carry `title`, albums and playlists carry `name`. Both land here.
        winrt::hstring Title;
        // Playlists have a curator rather than an artist; both land here.
        winrt::hstring Subtitle;
        winrt::hstring AlbumName;
        winrt::hstring ArtworkUrl;
        winrt::hstring SourceUrl;
        winrt::hstring Provider;
        winrt::hstring AlbumCatalogId;
        winrt::hstring ArtistCatalogId;
        winrt::hstring ReleaseDate;
        winrt::hstring Description;
        std::vector<winrt::hstring> GenreNames;
        std::int64_t DurationMs{ 0 };
        std::int32_t TrackCount{ 0 };
    };

    struct CatalogShelf
    {
        winrt::hstring Id;
        winrt::hstring Title;
        CatalogResourceType ItemType{ CatalogResourceType::Unknown };
        // Set when the shelf has a full chart page behind it, which is what the
        // "See all" affordance opens.
        std::optional<CatalogChartRef> Chart;
        std::vector<CatalogItem> Items;
    };

    struct CatalogDiscovery
    {
        winrt::hstring Storefront;
        winrt::hstring StorefrontName;
        winrt::hstring FetchedAt;
        std::vector<CatalogShelf> Shelves;
        std::vector<CatalogChartDescriptor> Charts;
        std::optional<CatalogCityChartGroups> CityChartGroups;
        std::optional<CatalogShelf> MoodActivityShelf;
        bool Stale{ false };
    };

    struct CatalogChartRequest
    {
        CatalogChartRef Ref;
        std::int32_t Limit{ 50 };
        std::int32_t Offset{ 0 };
    };

    struct CatalogChartPage
    {
        winrt::hstring Storefront;
        winrt::hstring Type;
        winrt::hstring Id;
        winrt::hstring Title;
        CatalogResourceType ItemType{ CatalogResourceType::Unknown };
        std::vector<CatalogItem> Items;
        std::optional<CatalogChartDescriptor> Descriptor;
        bool Stale{ false };
        // Absent when the service reported no further page.
        bool HasNextOffset{ false };
        std::int32_t NextOffset{ 0 };
    };

    // Album and playlist detail share a shape: one header resource plus tracks.
    struct CatalogResourceDetail
    {
        winrt::hstring Storefront;
        CatalogItem Resource;
        std::vector<CatalogItem> Tracks;
    };

    struct CatalogStorefront
    {
        winrt::hstring Code;
        winrt::hstring Name;
    };
}
