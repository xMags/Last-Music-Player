#include "pch.h"

#include "Backend/DetailSortPolicy.h"
#include "Backend/DownloadPolicy.h"
#include "Backend/PlaylistSuggestionPolicy.h"
#include "Backend/RecentSearchStore.h"
#include "Backend/SearchFacetPolicy.h"

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

    void TestDetailSortPolicy()
    {
        Expect(policy::KindSupportsCuratedOrder(L"PLAYLIST"),
            "playlist sort support should be case-insensitive");
        Expect(policy::KindSupportsCuratedOrder(L"album-collection"),
            "album collections should keep their stored order");
        Expect(!policy::KindSupportsCuratedOrder(L"artist"),
            "artist details have no curated position");
        Expect(policy::DefaultDetailSort(true) == policy::DetailSort::Curated,
            "curated collections should default to stored order");
        Expect(policy::DefaultDetailSort(false) == policy::DetailSort::DateAdded,
            "derived collections should default to recently added");
        Expect(policy::ParseDetailSort(L"custom", false) == policy::DetailSort::DateAdded,
            "a stale custom tag should fall back safely for derived collections");
        Expect(policy::ParseDetailSort(L"DURATION", true) == policy::DetailSort::Duration,
            "detail sort tags should parse case-insensitively");
        Expect(policy::DetailSortQueryValue(policy::DetailSort::Curated).empty(),
            "curated order should use the collection position fallback");
        Expect(policy::DetailSortLabel(policy::DetailSort::DateAdded) == L"Recently added",
            "date-added sort should use listener-facing copy");
    }

    void TestPlaylistSuggestionPolicy()
    {
        std::vector<policy::SuggestionCandidate> const candidates{
            { L"existing", L"artist-a", L"genre-a", 100 },
            { L"artist", L"artist-a", L"genre-z", 1 },
            { L"genre-high", L"artist-z", L"genre-a", 20 },
            { L"genre-low", L"artist-y", L"genre-a", 2 },
            { L"unmatched", L"artist-x", L"genre-x", 1000 },
        };
        policy::SuggestionContext const context{
            { L"existing" },
            { L"artist-a" },
            { L"genre-a" },
        };

        auto const ranked = policy::RankPlaylistSuggestions(candidates, context, 3);
        Expect(ranked.size() == 3, "three matching, non-member suggestions should remain");
        Expect(ranked[0] == 1, "an artist match should outrank genre-only matches");
        Expect(ranked[1] == 2 && ranked[2] == 3,
            "equal relevance should break ties by play count");
        Expect(policy::RankPlaylistSuggestions(candidates, context, 1).size() == 1,
            "the suggestion shelf should honor its limit");
        Expect(policy::RankPlaylistSuggestions(candidates, {}, 5).empty(),
            "an empty playlist context should not invent recommendations");
    }

    void TestRecentSearchStore()
    {
        std::vector<std::wstring> const history{ L"Rock", L"Jazz", L"Ambient" };
        auto const updated = policy::RecordRecentSearch(history, L"  rock  ", 3);
        Expect(updated.size() == 3, "recent-search history should stay bounded");
        Expect(updated[0] == L"rock" && updated[1] == L"Jazz" && updated[2] == L"Ambient",
            "recording should trim and case-insensitively move a query to the front");

        std::vector<std::wstring> const special{ L"line one\nline two", L"C:\\Music" };
        auto const encoded = policy::EncodeRecentSearches(special);
        auto const decoded = policy::DecodeRecentSearches(encoded);
        Expect(decoded == special, "escaped recent searches should round-trip exactly");
        Expect(policy::DecodeRecentSearches(encoded, 1).size() == 1,
            "decode should enforce the persisted history limit");
        Expect(policy::RecordRecentSearch(history, L"new", 0).empty(),
            "a zero-sized history should remain empty");
    }

    void TestSearchFacetPolicy()
    {
        std::vector<policy::SearchFacetInput> const albums{
            { L"Song 1", {}, L"Artist", L"Album", {}, L"album|artist", false, 0 },
            { L"Song 2", {}, L"Artist", L"ALBUM", {}, L"ALBUM|ARTIST", true, 0 },
            { L"Song 3", {}, L"Other", L"Elsewhere", {}, L"elsewhere|other", false, 0 },
            { L"No album", {}, L"Other", {}, {}, {}, false, 0 },
        };
        auto const facets = policy::BuildSearchFacets(
            albums,
            policy::SearchFacetKind::Album,
            10);
        Expect(facets.size() == 2, "album facets should group case-insensitively and skip blanks");
        Expect(facets[0].Title == L"Album" && facets[0].Count == 2,
            "the first spelling and provider order should remain stable");
        Expect(facets[0].InLibrary,
            "a facet should be in-library when any grouped result is in-library");

        auto const limited = policy::BuildSearchFacets(
            albums,
            policy::SearchFacetKind::Album,
            1);
        Expect(limited.size() == 1 && limited[0].Count == 2,
            "facet limits should reject new groups without truncating an accepted group's count");

        std::vector<policy::SearchFacetInput> const artists{
            { L"A", {}, L"First", {}, {}, L"first", false, 0 },
            { L"B", {}, L"First", {}, {}, L"FIRST", false, 0 },
            { L"C", {}, {}, {}, {}, {}, false, 0 },
        };
        auto const artistFacets = policy::BuildSearchFacets(
            artists,
            policy::SearchFacetKind::Artist,
            5);
        Expect(artistFacets.size() == 1 && artistFacets[0].Count == 2,
            "artist facets should group matches and skip unnamed artists");
    }

    void TestDownloadPolicy()
    {
        Expect(policy::AggregateDownloadState({
            policy::DownloadItemState::Completed,
            policy::DownloadItemState::Downloading }) == policy::DownloadItemState::Downloading,
            "an active child should make its collection active");
        Expect(policy::AggregateDownloadState({
            policy::DownloadItemState::Completed,
            policy::DownloadItemState::Queued }) == policy::DownloadItemState::Queued,
            "queued work should keep a partially completed collection queued");
        Expect(policy::AggregateDownloadState({
            policy::DownloadItemState::Completed,
            policy::DownloadItemState::Failed }) == policy::DownloadItemState::Failed,
            "a failed child should surface after no runnable work remains");
        Expect(policy::DownloadExtensionForMediaType(L"audio/FLAC") == L".flac",
            "media-type parsing should be case-insensitive");
        Expect(policy::DownloadExtensionForMediaType(L"application/octet-stream") == L".audio",
            "unknown media types should use a neutral extension");
        Expect(policy::DownloadProgressPercent(150, 100) == 100.0,
            "progress should clamp oversized responses");
        Expect(policy::DownloadProgressPercent(1, 0) == 0.0,
            "unknown totals should not divide by zero");
        Expect(!policy::DownloadSchedulingAllowed(true, false, true, false, false, false),
            "Wi-Fi-only jobs should wait on a non-Wi-Fi connection");
        Expect(!policy::DownloadSchedulingAllowed(false, false, false, true, false, false),
            "battery-blocked jobs should wait while unplugged");
        Expect(policy::DownloadSchedulingAllowed(false, false, true, true, false, false),
            "explicit battery permission should allow an otherwise valid job");
        Expect(!policy::DownloadSchedulingAllowed(false, true, true, false, true, false),
            "global pause should override available network and power");
    }
}

int main()
{
    try
    {
        TestDetailSortPolicy();
        TestPlaylistSuggestionPolicy();
        TestRecentSearchStore();
        TestSearchFacetPolicy();
        TestDownloadPolicy();
        std::wcout << L"PlayerScreenPolicyTests passed" << std::endl;
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "PlayerScreenPolicyTests failed: " << error.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "PlayerScreenPolicyTests failed with an unknown exception" << std::endl;
        return 1;
    }
}
