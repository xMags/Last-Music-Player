#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace LastMusicPlayer::Frontend
{
    inline constexpr double kArtistHeaderBreakpoint = 767.0;
    inline constexpr double kArtistPortraitMinimum = 180.0;
    inline constexpr double kArtistPortraitMaximum = 260.0;
    inline constexpr double kArtistPortraitWidthFraction = 0.28;
    inline constexpr double kArtistShelfPageFraction = 0.85;
    inline constexpr double kArtistShelfEdgeEpsilon = 2.0;

    struct ArtistHeaderLayout
    {
        bool Stacked{ true };
        double PortraitSize{ 0.0 };
    };

    struct ArtistPageScrollState
    {
        double Vertical{ 0.0 };
        double LatestRelease{ 0.0 };
        double TopSongs{ 0.0 };
        double Albums{ 0.0 };
        double Singles{ 0.0 };
        double SimilarArtists{ 0.0 };
    };

    template <typename DetailT, typename IdentityT, typename AccountPlanT, typename TrackT>
    struct ArtistNavigationEntryState
    {
        DetailT Detail;
        IdentityT Identity;
        std::optional<AccountPlanT> AccountPlan;
        std::vector<TrackT> LocalTracks;
        bool LocalFallback{ false };
        ArtistPageScrollState ScrollState;
    };

    template <typename EntryT>
    struct ArtistNavigationSnapshotState
    {
        EntryT Current;
        std::vector<EntryT> BackStack;
        std::wstring OriginalDestination;
    };

    [[nodiscard]] inline ArtistHeaderLayout CalculateArtistHeaderLayout(
        double availableWidth) noexcept
    {
        if (!std::isfinite(availableWidth) || availableWidth <= 0.0)
        {
            return {};
        }

        return {
            availableWidth < kArtistHeaderBreakpoint,
            std::clamp(
                availableWidth * kArtistPortraitWidthFraction,
                kArtistPortraitMinimum,
                kArtistPortraitMaximum)
        };
    }

    // The provider detail contract preserves presentation order, and the first
    // release is the site's Latest Release. Local fallback releases are
    // synthesized from library grouping and have no truthful latest ordering.
    [[nodiscard]] inline std::optional<std::size_t> LatestArtistReleaseIndex(
        std::size_t releaseCount,
        bool localFallback) noexcept
    {
        if (localFallback || releaseCount == 0)
        {
            return std::nullopt;
        }
        return 0;
    }

    [[nodiscard]] inline double ClampArtistShelfOffset(
        double requestedOffset,
        double scrollableWidth) noexcept
    {
        if (!std::isfinite(scrollableWidth) || scrollableWidth <= 0.0)
        {
            return 0.0;
        }
        if (!std::isfinite(requestedOffset))
        {
            return 0.0;
        }
        return std::clamp(requestedOffset, 0.0, scrollableWidth);
    }

    [[nodiscard]] inline double NextArtistShelfOffset(
        double currentOffset,
        double viewportWidth,
        double scrollableWidth) noexcept
    {
        auto const current = ClampArtistShelfOffset(currentOffset, scrollableWidth);
        if (!std::isfinite(viewportWidth) || viewportWidth <= 0.0)
        {
            return current;
        }

        return ClampArtistShelfOffset(
            current + viewportWidth * kArtistShelfPageFraction,
            scrollableWidth);
    }

    [[nodiscard]] inline bool CanAdvanceArtistShelf(
        double currentOffset,
        double scrollableWidth) noexcept
    {
        if (!std::isfinite(scrollableWidth)
            || scrollableWidth <= kArtistShelfEdgeEpsilon)
        {
            return false;
        }

        auto const current = ClampArtistShelfOffset(currentOffset, scrollableWidth);
        return current < scrollableWidth - kArtistShelfEdgeEpsilon;
    }

    [[nodiscard]] inline ArtistPageScrollState ClampArtistPageScrollState(
        ArtistPageScrollState const& requested,
        ArtistPageScrollState const& scrollableExtents) noexcept
    {
        return {
            ClampArtistShelfOffset(requested.Vertical, scrollableExtents.Vertical),
            ClampArtistShelfOffset(requested.LatestRelease, scrollableExtents.LatestRelease),
            ClampArtistShelfOffset(requested.TopSongs, scrollableExtents.TopSongs),
            ClampArtistShelfOffset(requested.Albums, scrollableExtents.Albums),
            ClampArtistShelfOffset(requested.Singles, scrollableExtents.Singles),
            ClampArtistShelfOffset(requested.SimilarArtists, scrollableExtents.SimilarArtists),
        };
    }
}
