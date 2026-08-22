#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    // A library track being considered for the "Fits this playlist" shelf.
    // Text fields are compared verbatim, so the caller folds case once while
    // building these rather than on every comparison here.
    struct SuggestionCandidate
    {
        std::wstring Key;
        std::wstring Artist;
        std::wstring Genre;
        std::uint32_t PlayCount{};
    };

    // What the playlist already holds. Keys exclude existing members; artists
    // and genres are what a candidate is scored against. All folded by the
    // caller, and neither list needs to be deduplicated.
    struct SuggestionContext
    {
        std::vector<std::wstring> MemberKeys;
        std::vector<std::wstring> MemberArtists;
        std::vector<std::wstring> MemberGenres;
    };

    // Ranks candidates against the playlist and returns indices into
    // `candidates`, best first, at most `limit` of them.
    //
    // A candidate scores for sharing an artist with the playlist, and again,
    // less strongly, for sharing a genre. Candidates that match on neither are
    // dropped rather than padded in: a shelf headed "Fits this playlist" that
    // silently falls back to "most played" would be claiming a relationship
    // that was never computed. An empty or unmatched playlist therefore
    // returns nothing, and the caller hides the shelf.
    //
    // Ties break by play count, then by input order, so the result is stable
    // for a given library and does not reshuffle between openings.
    [[nodiscard]] std::vector<std::size_t> RankPlaylistSuggestions(
        std::vector<SuggestionCandidate> const& candidates,
        SuggestionContext const& context,
        std::size_t limit);
}
