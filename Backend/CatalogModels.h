#pragma once

#include <cstdint>
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

    struct CatalogItem
    {
        CatalogResourceType Type{ CatalogResourceType::Unknown };
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
        winrt::hstring ChartId;
        winrt::hstring ChartType;
        std::vector<CatalogItem> Items;
    };

    struct CatalogDiscovery
    {
        winrt::hstring Storefront;
        winrt::hstring FetchedAt;
        std::vector<CatalogShelf> Shelves;
    };

    struct CatalogChartPage
    {
        winrt::hstring Storefront;
        winrt::hstring Type;
        winrt::hstring Id;
        winrt::hstring Title;
        CatalogResourceType ItemType{ CatalogResourceType::Unknown };
        std::vector<CatalogItem> Items;
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
