#include "pch.h"
#include "Backend/TrackSearchPolicy.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>
#include <unordered_set>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr std::array<std::wstring_view, 12> kCuratedLandingGenres{
            L"Indie", L"Classical", L"Electronic", L"Ghazal", L"Jazz", L"Lo-fi",
            L"Pop", L"Hip-Hop", L"Rock", L"Chill", L"Bollywood", L"Dance"
        };

        constexpr std::array<std::wstring_view, 27> kNonGenreLabels{
            L"song", L"music", L"audio", L"track", L"people & blogs",
            L"entertainment", L"news & politics", L"education", L"film & animation",
            L"gaming", L"howto & style", L"science & technology", L"sports", L"comedy",
            L"travel & events", L"autos & vehicles", L"pets & animals",
            L"nonprofits & activism", L"trailers", L"shows", L"movies", L"remote",
            L"other", L"various", L"various artists", L"unknown", L"unknown genre"
        };

        std::wstring Trim(std::wstring_view value)
        {
            auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            });
            auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t character)
            {
                return std::iswspace(character) != 0;
            }).base();
            return first < last ? std::wstring(first, last) : std::wstring{};
        }

        std::wstring Fold(std::wstring_view value)
        {
            std::wstring folded{ value };
            std::transform(folded.begin(), folded.end(), folded.begin(), [](wchar_t character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return folded;
        }

        bool IsUsefulGenre(std::wstring const& foldedName) noexcept
        {
            return !foldedName.empty()
                && foldedName != L"uncategorized"
                && std::find(kNonGenreLabels.begin(), kNonGenreLabels.end(), foldedName)
                    == kNonGenreLabels.end();
        }
    }

    std::vector<BrowseLandingCategory> BrowseLandingCategories(
        std::vector<std::wstring> const& rankedGenres)
    {
        std::vector<BrowseLandingCategory> categories;
        categories.reserve(kBrowseLandingCategoryLimit);
        std::unordered_set<std::wstring> usedNames;

        auto append = [&](std::wstring_view rawName, bool curated)
        {
            if (categories.size() >= kBrowseLandingCategoryLimit)
            {
                return;
            }

            auto name = Trim(rawName);
            auto folded = Fold(name);
            if (!IsUsefulGenre(folded) || !usedNames.insert(folded).second)
            {
                return;
            }

            categories.push_back({ std::move(name), curated });
        };

        for (auto const& genre : rankedGenres)
        {
            append(genre, false);
            if (categories.size() >= kBrowseLandingCategoryLimit)
            {
                return categories;
            }
        }

        for (auto const genre : kCuratedLandingGenres)
        {
            append(genre, true);
        }
        return categories;
    }
}
