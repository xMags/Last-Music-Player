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

    enum class TopResultKind
    {
        Song,
        Album
    };

    struct TopSearchResult
    {
        // Index into the tracks passed in. For an album result this is the
        // first track of that album, which is what the card draws its artwork
        // and its play action from.
        std::size_t Index{};
        TopResultKind Kind{ TopResultKind::Song };
        // Album title when Kind is Album, so the caller does not have to fold
        // the winning track's fields again to label the card.
        std::wstring Title;
    };

    // Picks which result the top-result card should show. The local query
    // orders by date added and the remote provider by its own relevance, so
    // taking the first row made the card arbitrary: searching an exact song
    // title could surface the oldest unrelated hit that merely shared a word.
    //
    // Scores each candidate on how squarely the query lands on it: a whole
    // field beats a prefix beats a substring, and only among equally good
    // matches does the field itself decide, title over artist over album.
    // Ties keep the earlier candidate, which preserves whatever relevance
    // order the source already applied. An album wins only when the query
    // names it outright and the results carry more than one of its tracks,
    // which is the case the reference's ALBUM badge exists for.
    //
    // Returns false when there is nothing to show, so the caller can collapse
    // the card rather than render an empty one.
    [[nodiscard]] bool ChooseTopSearchResult(
        std::vector<SearchFacetInput> const& tracks,
        std::wstring const& query,
        TopSearchResult& result);
}
