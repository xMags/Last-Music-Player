#include "pch.h"
#include "Backend/TrackSearchPolicy.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <numeric>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        // Case-insensitive because a listener sorting by title does not think of
        // "abbey road" and "Abbey Road" as belonging in different places. Only
        // case is folded, not accents: the platform's locale-aware collation is
        // not available here, and folding accents by hand would reorder names
        // wrongly in the languages that rely on them.
        int CompareFolded(std::wstring const& left, std::wstring const& right) noexcept
        {
            auto const shared = (std::min)(left.size(), right.size());
            for (std::size_t index = 0; index < shared; ++index)
            {
                auto const leftChar = std::towlower(left[index]);
                auto const rightChar = std::towlower(right[index]);
                if (leftChar != rightChar)
                {
                    return leftChar < rightChar ? -1 : 1;
                }
            }
            if (left.size() == right.size())
            {
                return 0;
            }
            return left.size() < right.size() ? -1 : 1;
        }

        constexpr std::array<std::wstring_view, 4> kDefaultLandingLabels{
            L"Pop", L"Hip-Hop", L"Electronic", L"Chill"
        };
    }

    SearchResultSort ParseSearchResultSort(std::wstring_view tag) noexcept
    {
        if (tag == L"Title")
        {
            return SearchResultSort::Title;
        }
        if (tag == L"Artist")
        {
            return SearchResultSort::Artist;
        }
        if (tag == L"Duration")
        {
            return SearchResultSort::Duration;
        }
        return SearchResultSort::Relevance;
    }

    std::wstring_view SearchResultSortTag(SearchResultSort sort) noexcept
    {
        switch (sort)
        {
        case SearchResultSort::Title:
            return L"Title";
        case SearchResultSort::Artist:
            return L"Artist";
        case SearchResultSort::Duration:
            return L"Duration";
        case SearchResultSort::Relevance:
            break;
        }
        return L"Relevance";
    }

    std::wstring_view SearchResultSortLabel(SearchResultSort sort) noexcept
    {
        // The tags and the labels happen to match today, but they answer to
        // different owners: one is markup, the other is what a listener reads.
        return SearchResultSortTag(sort);
    }

    std::vector<std::size_t> SearchResultOrder(
        std::vector<SearchSortKey> const& keys,
        SearchResultSort sort)
    {
        std::vector<std::size_t> order(keys.size());
        std::iota(order.begin(), order.end(), std::size_t{ 0 });
        if (sort == SearchResultSort::Relevance)
        {
            return order;
        }

        // stable_sort so equal keys keep the relevance order they arrived in,
        // which leaves the server's ranking as the tie-breaker rather than an
        // arbitrary one.
        std::stable_sort(order.begin(), order.end(),
            [&keys, sort](std::size_t left, std::size_t right)
            {
                auto const& leftKey = keys[left];
                auto const& rightKey = keys[right];
                switch (sort)
                {
                case SearchResultSort::Duration:
                    return leftKey.DurationSeconds < rightKey.DurationSeconds;
                case SearchResultSort::Artist:
                    return CompareFolded(leftKey.Artist, rightKey.Artist) < 0;
                case SearchResultSort::Title:
                case SearchResultSort::Relevance:
                    break;
                }
                return CompareFolded(leftKey.Title, rightKey.Title) < 0;
            });
        return order;
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
