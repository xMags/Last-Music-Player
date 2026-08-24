#pragma once

#include "Backend/AccountMusicContract.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    inline constexpr wchar_t LibraryArtistsCapability[] = L"library-artists-v1";
    inline constexpr std::size_t MaxLibraryArtistSearchCharacters = 120;
    inline constexpr std::size_t MaxLibraryArtistPageSize = 100;
    inline constexpr std::size_t DefaultLibraryArtistPageSize = 50;
    inline constexpr std::size_t DefaultTopArtistLimit = 48;

    enum class LibraryArtistView
    {
        Default,
        Followed,
        All,
    };

    struct LibraryArtistListRequest
    {
        LibraryArtistView View{ LibraryArtistView::Default };
        std::wstring Query;
        std::size_t Limit{ DefaultLibraryArtistPageSize };
        std::wstring Cursor;
    };

    struct LibraryArtistIdentity
    {
        std::wstring CatalogId;
        std::wstring SourceUrl;
        std::wstring Name;
        std::wstring Provider;
    };

    struct LibraryArtist
    {
        // Public service identifier. This is the only identifier accepted by
        // follow and saved-track routes; database row identifiers never enter
        // the desktop contract.
        std::wstring Id;
        std::wstring Name;
        std::wstring Provider;
        std::wstring ProviderCatalogId;
        std::wstring ProviderSourceUrl;
        std::wstring AppleCatalogId;
        std::wstring ArtworkUrl;
        std::wstring HeroArtworkUrl;
        std::int64_t TrackCount{};
        std::int64_t AlbumCount{};
        std::int64_t PlayCount{};
        bool Followed{};
        bool Resolving{};
        LibraryArtistIdentity DetailIdentity;
    };

    struct ParsedLibraryArtistPage
    {
        bool Valid{};
        std::vector<LibraryArtist> Artists;
        std::wstring NextCursor;
        std::optional<std::int64_t> TotalCount;
    };

    [[nodiscard]] wchar_t const* LibraryArtistViewName(LibraryArtistView view) noexcept;
    [[nodiscard]] bool IsValidLibraryArtistPublicId(std::wstring const& id) noexcept;
    [[nodiscard]] std::optional<winrt::hstring> BuildLibraryArtistListPath(
        LibraryArtistListRequest const& request) noexcept;
    [[nodiscard]] ParsedLibraryArtistPage ParseLibraryArtistPage(
        winrt::hstring const& payload) noexcept;
    [[nodiscard]] std::optional<std::vector<AccountLibraryTrackDto>>
        ParseLibraryArtistTracks(winrt::hstring const& payload) noexcept;
}
