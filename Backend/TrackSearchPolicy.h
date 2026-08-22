#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace LastMusicPlayer::Backend
{
    // The rules the search surface follows, kept apart from the UI that applies
    // them so they can be tested without a window.
    //
    // Deliberately free of TrackInfo: that is a projected runtimeclass whose
    // generated headers only exist inside the app project, so a function taking
    // one could not be reached from a standalone test. Callers lift the few
    // fields these rules need into the plain structures below. The merge of
    // local and remote hits stays with the UI for the same reason, since its
    // dedupe key is built from a TrackInfo.

    // A query shorter than this is not sent anywhere. Both other clients use
    // two, and the landing page is what fills the gap while the user is still
    // typing the first character.
    // The redesigned surface treats every non-empty query as a search state.
    // Local SQLite search handles one character cheaply, and catalog mode only
    // sends one-character queries when the listener explicitly widens scope.
    inline constexpr std::size_t kMinimumSearchQueryLength = 1;

    enum class SearchResultSort
    {
        // Whatever order the results arrived in: local hits, then the server's
        // own ranking. The only sort that is not a rearrangement.
        Relevance,
        Title,
        Artist,
        Duration
    };

    // Unknown tags fall back to Relevance rather than throwing, since the tag
    // arrives from markup and a typo there should not take the page down.
    [[nodiscard]] SearchResultSort ParseSearchResultSort(std::wstring_view tag) noexcept;
    [[nodiscard]] std::wstring_view SearchResultSortTag(SearchResultSort sort) noexcept;
    [[nodiscard]] std::wstring_view SearchResultSortLabel(SearchResultSort sort) noexcept;

    // One row's sortable fields, lifted out of whatever the caller holds.
    struct SearchSortKey
    {
        std::wstring Title;
        std::wstring Artist;
        double DurationSeconds{ 0.0 };
    };

    // Indices into `keys` in display order, so the caller can reorder its own
    // rows without this code needing to know what a row is. Relevance returns
    // the identity order. Ties keep their original relative position, which is
    // what stops equal titles from shuffling every time the sort is reapplied.
    [[nodiscard]] std::vector<std::size_t> SearchResultOrder(
        std::vector<SearchSortKey> const& keys,
        SearchResultSort sort);

    // Up to eight labels for the empty-query landing: the listener's own ranked
    // genres, then their ranked artists when no genre is tagged, then a static
    // list so a brand new account still has somewhere to start.
    [[nodiscard]] std::vector<std::wstring> BrowseLandingLabels(
        std::vector<std::wstring> const& rankedGenres,
        std::vector<std::wstring> const& rankedArtists);

    inline constexpr std::size_t kBrowseLandingLabelLimit = 8;
}
