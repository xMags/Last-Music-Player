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

    namespace
    {
        // How squarely the query lands dominates, and which field it landed on
        // only separates candidates that matched equally well. Ordering it the
        // other way round let an incidental title substring ("A Song About
        // Tanu Sen") outrank the artist actually named by the query.
        constexpr int kMatchWhole = 100;
        constexpr int kMatchPrefix = 60;
        constexpr int kMatchSubstring = 20;
        constexpr int kFieldTitle = 30;
        constexpr int kFieldArtist = 20;
        constexpr int kFieldAlbum = 10;
        // Smaller than the gap between any two field tiers, so provenance only
        // settles otherwise equal candidates rather than promoting a worse match.
        constexpr int kInLibraryBonus = 5;

        int FieldScore(std::wstring const& folded, std::wstring const& query, int field)
        {
            if (folded.empty() || query.empty())
            {
                return 0;
            }
            if (folded == query)
            {
                return kMatchWhole + field;
            }
            if (folded.starts_with(query))
            {
                return kMatchPrefix + field;
            }
            if (folded.find(query) != std::wstring::npos)
            {
                return kMatchSubstring + field;
            }
            return 0;
        }
    }

    bool ChooseTopSearchResult(
        std::vector<SearchFacetInput> const& tracks,
        std::wstring const& query,
        TopSearchResult& result)
    {
        if (tracks.empty())
        {
            return false;
        }

        auto const foldedQuery = Fold(query);
        std::size_t best = 0;
        int bestScore = -1;
        for (std::size_t index = 0; index < tracks.size(); ++index)
        {
            auto const& track = tracks[index];
            auto score = (std::max)({
                FieldScore(Fold(track.Title), foldedQuery, kFieldTitle),
                FieldScore(Fold(track.Artist), foldedQuery, kFieldArtist),
                FieldScore(Fold(track.Album), foldedQuery, kFieldAlbum) });
            if (track.InLibrary)
            {
                score += kInLibraryBonus;
            }
            if (score > bestScore)
            {
                bestScore = score;
                best = index;
            }
        }

        result.Index = best;
        result.Kind = TopResultKind::Song;
        result.Title = tracks[best].Title;

        // An album takes the card only when the query names it outright and the
        // page is holding more than one of its tracks. One lone track from an
        // album is still best shown as that track.
        auto const& winner = tracks[best];
        auto const foldedAlbum = Fold(winner.Album);
        if (foldedAlbum.empty() || foldedAlbum != foldedQuery)
        {
            return true;
        }

        std::size_t albumTracks = 0;
        for (auto const& track : tracks)
        {
            if (Fold(track.Album) == foldedAlbum)
            {
                ++albumTracks;
            }
        }
        if (albumTracks > 1)
        {
            result.Kind = TopResultKind::Album;
            result.Title = winner.Album;
        }
        return true;
    }
}
