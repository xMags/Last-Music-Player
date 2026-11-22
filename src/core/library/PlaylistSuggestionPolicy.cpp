#include "core/library/PlaylistSuggestionPolicy.h"

#include <QSet>

#include <algorithm>

namespace lmp::core {

namespace {

// A shared artist is the stronger signal: a genre often covers a large share
// of a library, while an artist the listener already picked is a direct match.
constexpr int ArtistScore = 2;
constexpr int GenreScore = 1;

struct ScoredCandidate {
    qsizetype index = 0;
    int score = 0;
    std::uint32_t playCount = 0;
};

} // namespace

QList<qsizetype> rankPlaylistSuggestions(const QList<SuggestionCandidate>& candidates,
                                         const SuggestionContext& context, qsizetype limit)
{
    QList<qsizetype> ranked;
    if (candidates.isEmpty() || limit <= 0) {
        return ranked;
    }

    const QSet<QString> memberKeys(context.memberKeys.cbegin(), context.memberKeys.cend());
    QSet<QString> memberArtists(context.memberArtists.cbegin(), context.memberArtists.cend());
    QSet<QString> memberGenres(context.memberGenres.cbegin(), context.memberGenres.cend());
    // An empty artist or genre would otherwise match every other track that
    // also has none, which is not a relationship worth showing.
    memberArtists.remove(QString());
    memberGenres.remove(QString());

    QList<ScoredCandidate> scored;
    scored.reserve(candidates.size());
    for (qsizetype index = 0; index < candidates.size(); ++index) {
        const SuggestionCandidate& candidate = candidates[index];
        if (!candidate.key.isEmpty() && memberKeys.contains(candidate.key)) {
            continue;
        }

        int score = 0;
        if (!candidate.artist.isEmpty() && memberArtists.contains(candidate.artist)) {
            score += ArtistScore;
        }
        if (!candidate.genre.isEmpty() && memberGenres.contains(candidate.genre)) {
            score += GenreScore;
        }
        if (score == 0) {
            continue;
        }
        scored.append({index, score, candidate.playCount});
    }

    std::stable_sort(scored.begin(), scored.end(), [](const ScoredCandidate& left, const ScoredCandidate& right) {
        if (left.score != right.score) {
            return left.score > right.score;
        }
        return left.playCount > right.playCount;
    });

    const qsizetype take = std::min(limit, scored.size());
    ranked.reserve(take);
    for (qsizetype index = 0; index < take; ++index) {
        ranked.append(scored[index].index);
    }
    return ranked;
}

} // namespace lmp::core
