#pragma once

#include <cstddef>
#include <string>
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
    // Local SQLite search handles one character cheaply, and an available
    // configured remote mode merges its own results after the local page.
    inline constexpr std::size_t kMinimumSearchQueryLength = 1;

    struct BrowseLandingCategory
    {
        std::wstring Name;
        bool IsCurated{};
    };

    // The empty-query landing keeps useful library genres in their ranked
    // order, then fills the remaining positions with honest catalog-search
    // shortcuts. The UI uses IsCurated to distinguish navigation into a local
    // genre from a unified search with the same visible card treatment.
    [[nodiscard]] std::vector<BrowseLandingCategory> BrowseLandingCategories(
        std::vector<std::wstring> const& rankedGenres);

    inline constexpr std::size_t kBrowseLandingCategoryLimit = 12;
}
