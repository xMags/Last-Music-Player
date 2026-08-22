#include "pch.h"
#include "Backend/DetailSortPolicy.h"

#include <algorithm>
#include <cwctype>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        std::wstring Folded(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](wchar_t character)
                {
                    return static_cast<wchar_t>(std::towlower(character));
                });
            return value;
        }

        bool ContainsFolded(std::wstring const& field, std::wstring const& needle)
        {
            return Folded(field).find(needle) != std::wstring::npos;
        }
    }

    bool KindSupportsCuratedOrder(std::wstring const& detailKind)
    {
        auto const kind = Folded(detailKind);
        return kind == L"playlist"
            || kind == L"auto-playlist"
            || kind == L"album-collection";
    }

    DetailSort DefaultDetailSort(bool supportsCuratedOrder)
    {
        return supportsCuratedOrder ? DetailSort::Curated : DetailSort::DateAdded;
    }

    DetailSort ParseDetailSort(std::wstring const& tag, bool supportsCuratedOrder)
    {
        auto const folded = Folded(tag);
        if (folded == L"title")
        {
            return DetailSort::Title;
        }
        if (folded == L"artist")
        {
            return DetailSort::Artist;
        }
        if (folded == L"duration")
        {
            return DetailSort::Duration;
        }
        if (folded == L"dateadded")
        {
            return DetailSort::DateAdded;
        }
        if (folded == L"custom" && supportsCuratedOrder)
        {
            return DetailSort::Curated;
        }
        return DefaultDetailSort(supportsCuratedOrder);
    }

    std::wstring DetailSortQueryValue(DetailSort sort)
    {
        switch (sort)
        {
        case DetailSort::Title:
            return L"Title";
        case DetailSort::Artist:
            return L"Artist";
        case DetailSort::Duration:
            return L"Duration";
        case DetailSort::DateAdded:
            return L"DateAdded";
        case DetailSort::Curated:
        default:
            return {};
        }
    }

    std::wstring DetailSortLabel(DetailSort sort)
    {
        switch (sort)
        {
        case DetailSort::Title:
            return L"Title";
        case DetailSort::Artist:
            return L"Artist";
        case DetailSort::Duration:
            return L"Duration";
        case DetailSort::DateAdded:
            return L"Recently added";
        case DetailSort::Curated:
        default:
            return L"Custom order";
        }
    }

    std::vector<std::size_t> DetailTrackOrder(
        std::vector<DetailTrackSortKey> const& keys,
        std::wstring_view query,
        DetailSort sort)
    {
        auto const needle = Folded(std::wstring{ query });
        std::vector<std::size_t> order;
        order.reserve(keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index)
        {
            auto const& key = keys[index];
            if (needle.empty()
                || ContainsFolded(key.Title, needle)
                || ContainsFolded(key.Artist, needle)
                || ContainsFolded(key.Album, needle))
            {
                order.push_back(index);
            }
        }

        if (sort == DetailSort::Curated || sort == DetailSort::DateAdded)
        {
            // Catalog details have no date-added value. DateAdded is accepted
            // defensively as source order so a stale tag cannot scramble them.
            return order;
        }

        std::stable_sort(order.begin(), order.end(),
            [&keys, sort](std::size_t left, std::size_t right)
            {
                auto const& leftKey = keys[left];
                auto const& rightKey = keys[right];
                if (sort == DetailSort::Duration
                    && leftKey.DurationSeconds != rightKey.DurationSeconds)
                {
                    return leftKey.DurationSeconds > rightKey.DurationSeconds;
                }
                if (sort == DetailSort::Artist)
                {
                    auto const leftArtist = Folded(leftKey.Artist);
                    auto const rightArtist = Folded(rightKey.Artist);
                    if (leftArtist != rightArtist)
                    {
                        return leftArtist < rightArtist;
                    }
                }
                return Folded(leftKey.Title) < Folded(rightKey.Title);
            });
        return order;
    }
}
