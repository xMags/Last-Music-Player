#include "pch.h"
#include "Backend/TrackSearchPolicy.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr std::array<std::wstring_view, 4> kDefaultLandingLabels{
            L"Pop", L"Hip-Hop", L"Electronic", L"Chill"
        };
    }

    std::vector<std::wstring> BrowseLandingLabels(
        std::vector<std::wstring> const& rankedGenres,
        std::vector<std::wstring> const& rankedArtists)
    {
        // Whichever ranking has anything in it wins outright; the two are not
        // blended. A library with even one tagged genre is better described by
        // its genres than by a mixture of genres and artist names.
        auto const& preferred = rankedGenres.empty() ? rankedArtists : rankedGenres;

        std::vector<std::wstring> labels;
        labels.reserve((std::min)(preferred.size(), kBrowseLandingLabelLimit));
        for (auto const& label : preferred)
        {
            if (labels.size() >= kBrowseLandingLabelLimit)
            {
                break;
            }
            if (!label.empty())
            {
                labels.push_back(label);
            }
        }

        if (labels.empty())
        {
            for (auto const& label : kDefaultLandingLabels)
            {
                labels.emplace_back(label);
            }
        }
        return labels;
    }
}
