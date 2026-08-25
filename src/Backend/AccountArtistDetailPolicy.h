#pragma once

#include "Backend/ArtistLibraryPolicy.h"
#include "Backend/LibraryArtistContract.h"
#include "Backend/RemoteDetailContract.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    inline constexpr std::size_t MaxAppleCatalogIdCharacters = 256;

    enum class AccountArtistDetailSource
    {
        AppleMusic,
        LastMusic,
    };

    struct AccountArtistDetailTarget
    {
        AccountArtistDetailSource Source{ AccountArtistDetailSource::LastMusic };
        ArtistIdentity Identity;
    };

    struct AccountArtistDetailPlan
    {
        std::wstring LibraryArtistId;
        std::optional<ArtistIdentity> AppleIdentity;
        ArtistIdentity LastMusicIdentity;
        bool Followed{ false };
    };

    enum class AccountArtistDetailAttemptOutcome
    {
        Success,
        InvalidPayload,
        HttpNotFound,
        HttpFailure,
        Timeout,
        TransportFailure,
        Cancelled,
        Stale,
    };

    [[nodiscard]] bool IsValidAppleCatalogId(std::wstring const& value) noexcept;
    [[nodiscard]] AccountArtistDetailPlan BuildAccountArtistDetailPlan(
        LibraryArtist const& artist);
    [[nodiscard]] std::vector<AccountArtistDetailTarget> OrderedAccountArtistDetailTargets(
        AccountArtistDetailPlan const& plan);
    [[nodiscard]] bool AccountArtistDetailPlanMatches(
        AccountArtistDetailPlan const& plan,
        ArtistIdentity const& requested);
    [[nodiscard]] bool ShouldTryNextAccountArtistDetailTarget(
        AccountArtistDetailAttemptOutcome outcome) noexcept;
    [[nodiscard]] std::wstring AccountArtistDetailCacheDiscriminator(
        AccountArtistDetailTarget const& target);
    [[nodiscard]] RemoteDetailIdentity RemoteIdentityFor(
        AccountArtistDetailTarget const& target);
}
