#pragma once

#include <QList>
#include <QString>

namespace lmp::core {

// Rules of the search surface, kept apart from the screen that applies them.

// Every non-empty query is a search. The local database answers a single
// character cheaply, and a configured remote source adds its own results
// after the local ones.
inline constexpr qsizetype MinimumSearchQueryLength = 1;

// How many category cards the empty-query landing shows.
inline constexpr qsizetype BrowseLandingCategoryLimit = 12;

struct BrowseLandingCategory {
    QString name;
    // True for a built-in shortcut that runs a search; false for a genre that
    // exists in the library and opens its genre page.
    bool isCurated = false;
};

// The empty-query landing: the library's useful genres in their ranked order
// (trimmed, case-insensitively deduplicated, labels that are not genres
// dropped), then built-in genre shortcuts in a fixed order until the limit is
// reached. A shortcut whose name the library already has is skipped.
[[nodiscard]] QList<BrowseLandingCategory> browseLandingCategories(const QList<QString>& rankedGenres);

} // namespace lmp::core
