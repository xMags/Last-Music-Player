#include "pch.h"

#include "Backend/TrackSearchPolicy.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace policy = LastMusicPlayer::Backend;

namespace
{
    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestLandingRejectsNonGenresAndNormalizesNames()
    {
        auto const categories = policy::BrowseLandingCategories({
            L" ", L"song", L"MUSIC", L" audio ", L"Track", L"People & Blogs",
            L"Unknown Genre", L"  Jazz\t", L"jAzZ", L"Rock" });

        Expect(categories.size() == policy::kBrowseLandingCategoryLimit,
            "invalid metadata should be replaced by useful curated genres");
        Expect(categories[0].Name == L"Jazz" && !categories[0].IsCurated,
            "a real genre should be trimmed while retaining its source");
        Expect(categories[1].Name == L"Rock" && !categories[1].IsCurated,
            "case-folded duplicates should keep only their first ranked spelling");
    }

    void TestLandingPreservesRealRankingThenFillsInCuratedOrder()
    {
        auto const categories = policy::BrowseLandingCategories({ L"Rock", L"Jazz" });
        Expect(categories.size() == policy::kBrowseLandingCategoryLimit,
            "the landing should be filled to twelve categories");
        Expect(categories[0].Name == L"Rock" && categories[1].Name == L"Jazz",
            "real genres should retain their count-ranked order");
        Expect(!categories[0].IsCurated && !categories[1].IsCurated,
            "real genres should remain local navigation targets");
        Expect(categories[2].Name == L"Indie" && categories[2].IsCurated,
            "curated filling should begin in the approved stable order");
        Expect(categories[3].Name == L"Classical" && categories[4].Name == L"Electronic",
            "curated filling order should remain stable");
    }

    void TestLandingMarksActualCuratedNameAsReal()
    {
        auto const categories = policy::BrowseLandingCategories({ L"Indie" });
        Expect(categories[0].Name == L"Indie" && !categories[0].IsCurated,
            "an actual genre should take precedence over the curated shortcut with the same name");
        Expect(categories[1].Name == L"Classical" && categories[1].IsCurated,
            "the duplicated curated name should be skipped without disturbing later fill order");
    }

    void TestLandingUsesTwelveCuratedGenresForAnEmptyLibrary()
    {
        auto const categories = policy::BrowseLandingCategories({});
        std::vector<std::wstring> const expected{
            L"Indie", L"Classical", L"Electronic", L"Ghazal", L"Jazz", L"Lo-fi",
            L"Pop", L"Hip-Hop", L"Rock", L"Chill", L"Bollywood", L"Dance"
        };
        Expect(categories.size() == expected.size(),
            "an empty library should still show all twelve curated genres");
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            Expect(categories[index].Name == expected[index] && categories[index].IsCurated,
                "the empty-library curated order should be deterministic");
        }
    }

    void TestLandingCapsRealGenresAtTwelve()
    {
        std::vector<std::wstring> genres;
        for (int index = 0; index < 20; ++index)
        {
            genres.push_back(L"Genre" + std::to_wstring(index));
        }
        auto const categories = policy::BrowseLandingCategories(genres);
        Expect(categories.size() == policy::kBrowseLandingCategoryLimit,
            "the landing grid should hold at most twelve categories");
        Expect(categories.back().Name == L"Genre11" && !categories.back().IsCurated,
            "the cap should preserve the first twelve real genres");
    }

    void TestMinimumQueryLength()
    {
        Expect(policy::kMinimumSearchQueryLength == 1,
            "the redesigned Browse surface should search every non-empty query");
    }
}

int main()
{
    try
    {
        TestLandingRejectsNonGenresAndNormalizesNames();
        TestLandingPreservesRealRankingThenFillsInCuratedOrder();
        TestLandingMarksActualCuratedNameAsReal();
        TestLandingUsesTwelveCuratedGenresForAnEmptyLibrary();
        TestLandingCapsRealGenresAtTwelve();
        TestMinimumQueryLength();
        std::wcout << L"TrackSearchPolicyTests passed" << std::endl;
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "TrackSearchPolicyTests failed: " << error.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "TrackSearchPolicyTests failed with an unknown exception" << std::endl;
        return 1;
    }
}
