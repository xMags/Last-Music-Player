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

    void TestLandingPrefersGenresThenArtistsThenDefaults()
    {
        std::vector<std::wstring> const genres{ L"Rock", L"Jazz" };
        std::vector<std::wstring> const artists{ L"Someone", L"Another" };

        auto const fromGenres = policy::BrowseLandingLabels(genres, artists);
        Expect(fromGenres.size() == 2 && fromGenres[0] == L"Rock" && fromGenres[1] == L"Jazz",
            "genres should win outright when there are any");

        auto const fromArtists = policy::BrowseLandingLabels({}, artists);
        Expect(fromArtists.size() == 2 && fromArtists[0] == L"Someone",
            "artists should be used when no genre is tagged");

        auto const fallback = policy::BrowseLandingLabels({}, {});
        Expect(fallback.size() == 4, "a new account should still get the static list");
        Expect(fallback[0] == L"Pop" && fallback[1] == L"Hip-Hop"
            && fallback[2] == L"Electronic" && fallback[3] == L"Chill",
            "the static list should match the other clients");
    }

    void TestLandingCapsAndSkipsBlanks()
    {
        std::vector<std::wstring> genres;
        for (int index = 0; index < 20; ++index)
        {
            genres.push_back(L"Genre" + std::to_wstring(index));
        }
        Expect(policy::BrowseLandingLabels(genres, {}).size() == policy::kBrowseLandingLabelLimit,
            "the landing grid should hold at most eight labels");

        std::vector<std::wstring> const withBlank{ L"", L"Rock", L"" };
        auto const cleaned = policy::BrowseLandingLabels(withBlank, {});
        Expect(cleaned.size() == 1 && cleaned[0] == L"Rock",
            "a blank label should never become a card");

        // All-blank is indistinguishable from having nothing to offer, so the
        // static list has to take over rather than leaving an empty grid.
        std::vector<std::wstring> const allBlank{ L"", L"" };
        Expect(policy::BrowseLandingLabels(allBlank, {}).size() == 4,
            "labels that are all blank should fall through to the static list");
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
        TestLandingPrefersGenresThenArtistsThenDefaults();
        TestLandingCapsAndSkipsBlanks();
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
