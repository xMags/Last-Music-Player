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
}
