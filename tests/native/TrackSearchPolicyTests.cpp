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

    policy::SearchSortKey Key(wchar_t const* title, wchar_t const* artist, double seconds)
    {
        return policy::SearchSortKey{ title, artist, seconds };
    }

    std::vector<std::wstring> TitlesInOrder(
        std::vector<policy::SearchSortKey> const& keys,
        policy::SearchResultSort sort)
    {
        std::vector<std::wstring> titles;
        for (auto index : policy::SearchResultOrder(keys, sort))
        {
            titles.push_back(keys[index].Title);
        }
        return titles;
    }

    void TestSortTagRoundTrip()
    {
        Expect(policy::ParseSearchResultSort(L"Relevance") == policy::SearchResultSort::Relevance,
            "Relevance tag should parse");
        Expect(policy::ParseSearchResultSort(L"Title") == policy::SearchResultSort::Title,
            "Title tag should parse");
        Expect(policy::ParseSearchResultSort(L"Artist") == policy::SearchResultSort::Artist,
            "Artist tag should parse");
        Expect(policy::ParseSearchResultSort(L"Duration") == policy::SearchResultSort::Duration,
            "Duration tag should parse");

        // Markup can carry a typo; the surface must stay up and fall back to
        // the order the results arrived in.
        Expect(policy::ParseSearchResultSort(L"") == policy::SearchResultSort::Relevance,
            "an empty tag should fall back to Relevance");
        Expect(policy::ParseSearchResultSort(L"title") == policy::SearchResultSort::Relevance,
            "tags are matched exactly, so a lowercase tag falls back");

        for (auto sort : { policy::SearchResultSort::Relevance, policy::SearchResultSort::Title,
                           policy::SearchResultSort::Artist, policy::SearchResultSort::Duration })
        {
            Expect(policy::ParseSearchResultSort(policy::SearchResultSortTag(sort)) == sort,
                "every sort's own tag should parse back to it");
        }
    }

    void TestRelevanceKeepsArrivalOrder()
    {
        std::vector<policy::SearchSortKey> keys{
            Key(L"Zebra", L"Beta", 30.0),
            Key(L"Apple", L"Alpha", 10.0),
        };
        auto const titles = TitlesInOrder(keys, policy::SearchResultSort::Relevance);
        Expect(titles.size() == 2, "relevance should return every row");
        Expect(titles[0] == L"Zebra" && titles[1] == L"Apple",
            "relevance should leave the merge order untouched");
    }

    void TestTitleAndArtistSortIgnoreCase()
    {
        std::vector<policy::SearchSortKey> keys{
            Key(L"banana", L"delta", 10.0),
            Key(L"Apple", L"Echo", 20.0),
            Key(L"cherry", L"charlie", 30.0),
        };

        auto const byTitle = TitlesInOrder(keys, policy::SearchResultSort::Title);
        Expect(byTitle[0] == L"Apple" && byTitle[1] == L"banana" && byTitle[2] == L"cherry",
            "title sort should be case-insensitive ascending");

        auto const byArtist = TitlesInOrder(keys, policy::SearchResultSort::Artist);
        Expect(byArtist[0] == L"cherry" && byArtist[1] == L"banana" && byArtist[2] == L"Apple",
            "artist sort should be case-insensitive ascending");
    }

    void TestDurationSortAscending()
    {
        std::vector<policy::SearchSortKey> keys{
            Key(L"Long", L"A", 300.0),
            Key(L"Missing", L"B", 0.0),
            Key(L"Short", L"C", 42.5),
        };
        auto const titles = TitlesInOrder(keys, policy::SearchResultSort::Duration);
        Expect(titles[0] == L"Missing" && titles[1] == L"Short" && titles[2] == L"Long",
            "duration sort should be ascending, with an unknown duration first");
    }

    void TestEqualKeysKeepRelevanceOrder()
    {
        // Two hits for the same song from different sources must not swap
        // places every time the sort is reapplied.
        std::vector<policy::SearchSortKey> keys{
            Key(L"Same", L"First", 10.0),
            Key(L"Same", L"Second", 20.0),
            Key(L"Same", L"Third", 30.0),
        };
        std::vector<std::wstring> artists;
        for (auto index : policy::SearchResultOrder(keys, policy::SearchResultSort::Title))
        {
            artists.push_back(keys[index].Artist);
        }
        Expect(artists[0] == L"First" && artists[1] == L"Second" && artists[2] == L"Third",
            "equal titles should keep the order they arrived in");
    }

    void TestSortHandlesEmptyInput()
    {
        std::vector<policy::SearchSortKey> keys;
        Expect(policy::SearchResultOrder(keys, policy::SearchResultSort::Title).empty(),
            "sorting nothing should produce nothing");
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
        TestSortTagRoundTrip();
        TestRelevanceKeepsArrivalOrder();
        TestTitleAndArtistSortIgnoreCase();
        TestDurationSortAscending();
        TestEqualKeysKeepRelevanceOrder();
        TestSortHandlesEmptyInput();
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
