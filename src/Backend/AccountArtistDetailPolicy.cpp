#include "pch.h"
#include "Backend/AccountArtistDetailPolicy.h"

#include <algorithm>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        bool ProvidersMatch(std::wstring const& left, std::wstring const& right)
        {
            auto const normalizedLeft = NormalizeArtistName(left);
            auto const normalizedRight = NormalizeArtistName(right);
            return normalizedLeft.empty()
                || normalizedRight.empty()
                || normalizedLeft == normalizedRight;
        }

        template <typename Predicate>
        bool AnyPlanIdentity(
            AccountArtistDetailPlan const& plan,
            Predicate&& predicate)
        {
            return (plan.AppleIdentity && predicate(*plan.AppleIdentity))
                || predicate(plan.LastMusicIdentity);
        }
    }

    bool IsValidAppleCatalogId(std::wstring const& value) noexcept
    {
        return !value.empty()
            && value.size() <= MaxAppleCatalogIdCharacters
            && std::all_of(value.begin(), value.end(), [](wchar_t character)
            {
                return character >= L'0' && character <= L'9';
            });
    }

    AccountArtistDetailPlan BuildAccountArtistDetailPlan(
        LibraryArtist const& artist)
    {
        AccountArtistDetailPlan plan;
        plan.LibraryArtistId = artist.Id;
        plan.Followed = artist.Followed;
        if (IsValidAppleCatalogId(artist.AppleCatalogId))
        {
            plan.AppleIdentity = ArtistIdentity{
                artist.AppleCatalogId,
                {},
                artist.Name,
                L"apple-music"
            };
        }

        plan.LastMusicIdentity = ArtistIdentity{
            artist.DetailIdentity.CatalogId,
            artist.DetailIdentity.SourceUrl,
            artist.DetailIdentity.Name.empty() ? artist.Name : artist.DetailIdentity.Name,
            artist.DetailIdentity.Provider.empty() ? artist.Provider : artist.DetailIdentity.Provider
        };
        return plan;
    }

    std::vector<AccountArtistDetailTarget> OrderedAccountArtistDetailTargets(
        AccountArtistDetailPlan const& plan)
    {
        std::vector<AccountArtistDetailTarget> targets;
        targets.reserve(plan.AppleIdentity ? 2 : 1);
        if (plan.AppleIdentity)
        {
            targets.push_back({ AccountArtistDetailSource::AppleMusic, *plan.AppleIdentity });
        }
        targets.push_back({ AccountArtistDetailSource::LastMusic, plan.LastMusicIdentity });
        return targets;
    }

    bool AccountArtistDetailPlanMatches(
        AccountArtistDetailPlan const& plan,
        ArtistIdentity const& requested)
    {
        if (!requested.CatalogId.empty())
        {
            return AnyPlanIdentity(plan, [&](ArtistIdentity const& candidate)
            {
                return !candidate.CatalogId.empty()
                    && candidate.CatalogId == requested.CatalogId
                    && ProvidersMatch(candidate.Provider, requested.Provider);
            });
        }
        if (!requested.SourceUrl.empty())
        {
            return AnyPlanIdentity(plan, [&](ArtistIdentity const& candidate)
            {
                return !candidate.SourceUrl.empty()
                    && candidate.SourceUrl == requested.SourceUrl;
            });
        }

        auto const requestedName = NormalizeArtistName(requested.Name);
        if (requestedName.empty())
        {
            return false;
        }
        if (requestedName == NormalizeArtistName(plan.LastMusicIdentity.Name))
        {
            return true;
        }
        return plan.AppleIdentity
            && requestedName == NormalizeArtistName(plan.AppleIdentity->Name);
    }

    bool ShouldTryNextAccountArtistDetailTarget(
        AccountArtistDetailAttemptOutcome outcome) noexcept
    {
        switch (outcome)
        {
        case AccountArtistDetailAttemptOutcome::InvalidPayload:
        case AccountArtistDetailAttemptOutcome::HttpNotFound:
        case AccountArtistDetailAttemptOutcome::HttpFailure:
        case AccountArtistDetailAttemptOutcome::Timeout:
        case AccountArtistDetailAttemptOutcome::TransportFailure:
            return true;
        default:
            return false;
        }
    }

    std::wstring AccountArtistDetailCacheDiscriminator(
        AccountArtistDetailTarget const& target)
    {
        if (target.Source == AccountArtistDetailSource::AppleMusic)
        {
            return L"apple-music";
        }
        auto const provider = NormalizeArtistName(target.Identity.Provider);
        return provider.empty()
            ? std::wstring{ L"last-music" }
            : L"last-music|" + provider;
    }

    RemoteDetailIdentity RemoteIdentityFor(AccountArtistDetailTarget const& target)
    {
        return {
            winrt::hstring(target.Identity.CatalogId),
            winrt::hstring(target.Identity.SourceUrl),
            winrt::hstring(target.Identity.Name)
        };
    }
}
