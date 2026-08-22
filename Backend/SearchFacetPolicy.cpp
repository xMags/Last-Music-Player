#include "pch.h"
#include "Backend/SearchFacetPolicy.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        struct Aggregate
        {
            SearchFacet facet;
            std::wstring normalizedKey;
        };

        std::wstring Fold(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                [](wchar_t character)
                {
                    return static_cast<wchar_t>(std::towlower(character));
                });
            return value;
        }
    }

    std::vector<SearchFacet> BuildSearchFacets(
        std::vector<SearchFacetInput> const& tracks,
        SearchFacetKind kind,
        std::size_t limit)
    {
        std::vector<SearchFacet> result;
        if (limit == 0)
        {
            return result;
        }

        std::unordered_map<std::wstring, std::size_t> positions;
        positions.reserve(tracks.size());
        for (auto const& track : tracks)
        {
            std::wstring title;
            std::wstring subtitle;
            switch (kind)
            {
            case SearchFacetKind::Album:
                title = track.Album;
                subtitle = track.Artist;
                break;
            case SearchFacetKind::Artist:
                title = track.Artist;
                subtitle = track.Count == 0 ? L"Artist" : std::to_wstring(track.Count) + L" songs";
                break;
            case SearchFacetKind::Playlist:
                title = track.Title;
                subtitle = track.Subtitle;
                break;
            }
            if (title.empty())
            {
                continue;
            }

            auto key = Fold(track.Key.empty() ? title : track.Key);
            auto found = positions.find(key);
            if (found == positions.end())
            {
                if (result.size() >= limit)
                {
                    continue;
                }
                SearchFacet facet;
                facet.Title = title;
                facet.Subtitle = subtitle;
                facet.Key = track.Key.empty() ? title : track.Key;
                facet.Kind = kind;
                facet.Count = 1;
                facet.InLibrary = track.InLibrary;
                positions.emplace(std::move(key), result.size());
                result.push_back(std::move(facet));
                continue;
            }

            auto& facet = result[found->second];
            ++facet.Count;
            facet.InLibrary = facet.InLibrary || track.InLibrary;
            if (facet.Subtitle.empty())
            {
                facet.Subtitle = subtitle;
            }
        }
        return result;
    }
}
