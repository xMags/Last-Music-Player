#pragma once

#include <string>

namespace LastMusicPlayer::Backend
{
    // Sort orders offered by the collection detail page (playlist, album,
    // artist, genre).
    enum class DetailSort
    {
        // The collection's own arrangement. Only a playlist or an album
        // collection has one: those store an explicit position per member.
        Curated,
        DateAdded,
        Title,
        Artist,
        Duration
    };

    // Whether a detail kind has a curated position to sort by. Artist and
    // genre pages are filtered views over the whole library rather than
    // curated collections, so Curated is not offered for them.
    [[nodiscard]] bool KindSupportsCuratedOrder(std::wstring const& detailKind);

    [[nodiscard]] DetailSort DefaultDetailSort(bool supportsCuratedOrder);

    // Maps a menu item's tag to a sort. Unrecognised text, and Curated asked
    // for by a kind that has no curated order, both fall back to the default
    // for that kind, so a stale tag can never produce an empty ordering.
    [[nodiscard]] DetailSort ParseDetailSort(
        std::wstring const& tag,
        bool supportsCuratedOrder);

    // The value to place in TrackQuery::Sort. Curated maps to an empty string:
    // the curated-collection query branches read that as "keep the stored
    // position", which is what their ORDER BY falls back to.
    [[nodiscard]] std::wstring DetailSortQueryValue(DetailSort sort);

    [[nodiscard]] std::wstring DetailSortLabel(DetailSort sort);
}
