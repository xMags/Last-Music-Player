#include "pch.h"
#include "Backend/PlaylistSuggestionPolicy.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        // Sharing an artist with the playlist is the stronger signal: a genre
        // is often broad enough to cover a large share of a library, while an
        // artist the listener already picked is a direct match.
        constexpr int kArtistScore = 2;
        constexpr int kGenreScore = 1;
    }

    std::vector<std::size_t> RankPlaylistSuggestions(
        std::vector<SuggestionCandidate> const& candidates,
        SuggestionContext const& context,
        std::size_t limit)
    {
        std::vector<std::size_t> ranked;
        if (candidates.empty() || limit == 0)
        {
            return ranked;
        }

        std::unordered_set<std::wstring> memberKeys(
            context.MemberKeys.begin(), context.MemberKeys.end());
        std::unordered_set<std::wstring> memberArtists(
            context.MemberArtists.begin(), context.MemberArtists.end());
        std::unordered_set<std::wstring> memberGenres(
            context.MemberGenres.begin(), context.MemberGenres.end());
        // An empty artist or genre would otherwise match every other track
        // that also has none, which is not a relationship worth surfacing.
        memberArtists.erase(std::wstring{});
        memberGenres.erase(std::wstring{});

        struct Scored
        {
            std::size_t Index{};
            int Score{};
            std::uint32_t PlayCount{};
        };

        std::vector<Scored> scored;
        scored.reserve(candidates.size());
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            auto const& candidate = candidates[index];
            if (!candidate.Key.empty() && memberKeys.contains(candidate.Key))
            {
                continue;
            }

            int score = 0;
            if (!candidate.Artist.empty() && memberArtists.contains(candidate.Artist))
            {
                score += kArtistScore;
            }
            if (!candidate.Genre.empty() && memberGenres.contains(candidate.Genre))
            {
                score += kGenreScore;
            }
            if (score == 0)
            {
                continue;
            }
            scored.push_back({ index, score, candidate.PlayCount });
        }

        std::stable_sort(scored.begin(), scored.end(),
            [](Scored const& left, Scored const& right)
            {
                if (left.Score != right.Score)
                {
                    return left.Score > right.Score;
                }
                return left.PlayCount > right.PlayCount;
            });

        auto const take = (std::min)(limit, scored.size());
        ranked.reserve(take);
        for (std::size_t index = 0; index < take; ++index)
        {
            ranked.push_back(scored[index].Index);
        }
        return ranked;
    }
}
