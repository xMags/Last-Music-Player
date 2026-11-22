#pragma once

#include <QList>
#include <QString>

#include <cstdint>

namespace lmp::core {

// A library track considered for the "Fits this playlist" shelf. Text is
// compared exactly, so the caller folds case once while building these.
struct SuggestionCandidate {
    QString key;
    QString artist;
    QString genre;
    std::uint32_t playCount = 0;
};

// What the playlist already holds, folded by the caller. Member keys are
// excluded; artists and genres are what candidates score against. None of the
// lists needs to be deduplicated.
struct SuggestionContext {
    QList<QString> memberKeys;
    QList<QString> memberArtists;
    QList<QString> memberGenres;
};

// Indices into `candidates`, best first, at most `limit`.
//
// Sharing an artist with the playlist scores higher than sharing a genre.
// Candidates that share neither are dropped rather than padded in, so the
// shelf never claims a relationship that was not computed; an empty or
// unmatched playlist returns nothing and the caller hides the shelf. Ties
// break by play count, then by input order, so the result is stable.
[[nodiscard]] QList<qsizetype> rankPlaylistSuggestions(const QList<SuggestionCandidate>& candidates,
                                                       const SuggestionContext& context, qsizetype limit);

} // namespace lmp::core
