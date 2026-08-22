#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    enum class SearchFacetKind
    {
        Album,
        Artist,
        Playlist
    };

    struct SearchFacetInput
    {
        std::wstring Title;
        std::wstring Subtitle;
        std::wstring Artist;
        std::wstring Album;
        std::wstring Genre;
        std::wstring Key;
        bool InLibrary{};
        std::size_t Count{};
    };

    struct SearchFacet
    {
        std::wstring Title;
        std::wstring Subtitle;
        std::wstring Key;
        SearchFacetKind Kind{ SearchFacetKind::Album };
        std::size_t Count{};
        bool InLibrary{};
    };

    // Groups track-shaped search results into display facets. Empty album or
    // artist names are ignored instead of producing a card titled "Unknown".
    // Stable first-seen ordering preserves the provider's relevance order.
    [[nodiscard]] std::vector<SearchFacet> BuildSearchFacets(
        std::vector<SearchFacetInput> const& tracks,
        SearchFacetKind kind,
        std::size_t limit);
}
