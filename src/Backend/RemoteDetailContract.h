#pragma once

#include "Backend/CatalogModels.h"

#include <optional>
#include <string_view>
#include <vector>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    inline constexpr wchar_t ArtistDetailCapability[] = L"artist-detail-v1";
    inline constexpr wchar_t AlbumDetailCapability[] = L"album-detail-v1";

    struct RemoteDetailIdentity
    {
        winrt::hstring CatalogId;
        winrt::hstring SourceUrl;
        winrt::hstring Name;
    };

    enum class RemoteReleaseKind
    {
        Unknown,
        Album,
        Single,
        Ep,
    };

    struct RemoteArtistSummary
    {
        winrt::hstring CatalogId;
        winrt::hstring Name;
        winrt::hstring Provider;
        winrt::hstring SourceUrl;
        winrt::hstring ArtworkUrl;
    };

    struct RemoteReleaseSummary
    {
        CatalogItem Release;
        RemoteReleaseKind Kind{ RemoteReleaseKind::Unknown };
    };

    struct RemoteArtistDetail
    {
        RemoteArtistSummary Artist;
        winrt::hstring HeroArtworkUrl;
        winrt::hstring Biography;
        std::vector<winrt::hstring> GenreNames;
        std::vector<CatalogItem> TopSongs;
        std::vector<RemoteReleaseSummary> Releases;
        std::vector<RemoteArtistSummary> RelatedArtists;
        bool Stale{ false };
        winrt::hstring FetchedAt;
    };

    struct RemoteAlbumDetail
    {
        RemoteReleaseSummary Release;
        std::vector<CatalogItem> Tracks;
        bool Stale{ false };
        winrt::hstring FetchedAt;
    };

    // Chooses exactly one identity in contract order. Sending only the selected
    // identity prevents a failed explicit catalog id from silently degrading to
    // an unrelated same-name result.
    std::optional<winrt::hstring> BuildRemoteDetailQuery(
        RemoteDetailIdentity const& identity) noexcept;

    bool ProviderSupportsCapability(
        winrt::hstring const& payload,
        std::wstring_view capability) noexcept;

    std::optional<RemoteArtistDetail> ParseRemoteArtistDetail(
        winrt::hstring const& payload) noexcept;
    std::optional<RemoteAlbumDetail> ParseRemoteAlbumDetail(
        winrt::hstring const& payload) noexcept;
}
