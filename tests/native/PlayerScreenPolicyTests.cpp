#include "pch.h"

#include "Backend/DetailSortPolicy.h"
#include "Backend/BrowseGroupingPolicy.h"
#include "Backend/DownloadPolicy.h"
#include "Backend/GenreArtworkRecipe.h"
#include "Backend/PlaylistSuggestionPolicy.h"
#include "Backend/RecentSearchStore.h"
#include "Backend/ResumePositionPolicy.h"

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

        std::vector<policy::DetailTrackSortKey> const tracks{
            { L"Zulu", L"Beta", L"One", 120.0 },
            { L"alpha", L"Gamma", L"Two", 300.0 },
            { L"Bravo", L"beta", L"Three", 180.0 },
        };
        Expect(policy::DetailTrackOrder(tracks, L"", policy::DetailSort::Curated)
                == std::vector<std::size_t>{ 0, 1, 2 },
            "custom order should preserve the catalog's source order");
        Expect(policy::DetailTrackOrder(tracks, L"two", policy::DetailSort::Curated)
                == std::vector<std::size_t>{ 1 },
            "detail find should match album text case-insensitively");
        Expect(policy::DetailTrackOrder(tracks, L"BETA", policy::DetailSort::Title)
                == std::vector<std::size_t>{ 2, 0 },
            "filtered title sort should be case-insensitive");
        Expect(policy::DetailTrackOrder(tracks, L"", policy::DetailSort::Artist)
                == std::vector<std::size_t>{ 2, 0, 1 },
            "artist ties should be ordered by title");
        Expect(policy::DetailTrackOrder(tracks, L"", policy::DetailSort::Duration)
                == std::vector<std::size_t>{ 1, 2, 0 },
            "duration sort should put the longest track first");
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

    void TestGenreArtworkRecipe()
    {
        std::vector<std::wstring> const expectedNames{
            L"Pop", L"Hip-Hop", L"Electronic", L"Chill", L"Rock", L"Metal",
            L"Jazz", L"Blues", L"Classical", L"Country", L"R&B", L"Soul",
            L"Folk", L"Reggae", L"Punk", L"Indie", L"Dance", L"House",
            L"Techno", L"Ambient", L"Lo-fi", L"Soundtrack", L"World", L"Latin",
            L"Bollywood", L"Ghazal", L"Unknown Genre"
        };
        auto const& recipes = policy::AllGenreArtworkRecipes();
        Expect(recipes.size() == expectedNames.size(),
            "the recipe catalog should contain every handoff genre exactly once");
        for (std::size_t index = 0; index < expectedNames.size(); ++index)
        {
            Expect(recipes[index].Name == expectedNames[index],
                "genre recipe order and names should match the handoff");
            Expect(!recipes[index].Primitives.empty(),
                "every genre recipe should contain a visible mark");
            Expect(policy::ResolveGenreArtworkRecipe(L" \t" + expectedNames[index] + L"\r\n").Name
                    == expectedNames[index],
                "genre recipe lookup should trim surrounding whitespace");
        }

        Expect(policy::ResolveGenreArtworkRecipe(L"hIp-HoP").Name == L"Hip-Hop",
            "genre recipe lookup should fold case");
        Expect(policy::ResolveGenreArtworkRecipe(L"").Name == L"Unknown Genre",
            "an empty genre should use the unknown artwork");
        Expect(policy::ResolveGenreArtworkRecipe(L"Shoegaze").Name == L"Unknown Genre",
            "an unmatched genre should use the unknown artwork");

        auto const& pop = policy::ResolveGenreArtworkRecipe(L"Pop");
        Expect(pop.GradientStart == 0xFFFF5FBF && pop.GradientEnd == 0xFFA855F7,
            "Pop should preserve the exact handoff gradient");
        Expect(pop.Primitives.size() == 2
                && pop.Primitives[0].Kind == policy::GenreArtworkPrimitiveKind::Ring
                && pop.Primitives[0].X == 11.0
                && pop.Primitives[0].Y == 11.0
                && pop.Primitives[0].Width == 74.0
                && pop.Primitives[0].StrokeWidth == 3.0,
            "outlined marks should preserve border-box geometry");

        auto const& folk = policy::ResolveGenreArtworkRecipe(L"Folk");
        Expect(folk.Primitives.size() == 1
                && folk.Primitives[0].Kind == policy::GenreArtworkPrimitiveKind::Triangle
                && folk.Primitives[0].X == 6.0
                && folk.Primitives[0].Y == 14.0
                && folk.Primitives[0].Width == 84.0
                && folk.Primitives[0].Height == 70.0,
            "the Folk mark should retain its native triangle bounds");

        auto const& unknown = policy::ResolveGenreArtworkRecipe(L"Unknown Genre");
        Expect(unknown.GradientStart == 0xFFCBD5E1
                && unknown.GradientEnd == 0xFF64748B
                && unknown.Primitives[0].Dashed,
            "the unknown fallback should preserve its neutral palette and dashed ring");
    }

    void TestBrowseGroupingPolicy()
    {
        using Kind = policy::BrowseResultKind;
        std::vector<policy::BrowseResultInput> const inputs{
            { Kind::Song, L"Neon Sky", L"Aurora", L"First Light", L"2024", L"3:20", L"song-1", 0 },
            { Kind::Song, L"Afterglow", L"Aurora", L"First Light", L"2024", L"4:01", L"song-2", 0 },
            { Kind::Song, L"Rock Song", L"The Band", L"Stone", {}, L"2:59", L"song-3", 0 },
            { Kind::Playlist, L"Aurora Picks", L"Playlist · 9 tracks", {}, {}, {}, L"playlist-1", 9 },
            { Kind::Playlist, L"Road Trip", L"Aurora curator", {}, {}, {}, L"playlist-2", 12 },
        };

        auto const grouped = policy::BuildGroupedBrowseResults(inputs, L"aurora", false);
        Expect(grouped.Groups.size() == 4,
            "a shared identity query should produce every non-empty entity group");
        Expect(grouped.Groups[0].Kind == Kind::Song
                && grouped.Groups[1].Kind == Kind::Album
                && grouped.Groups[2].Kind == Kind::Playlist
                && grouped.Groups[3].Kind == Kind::Artist,
            "Browse groups should stay ordered songs, albums, playlists, artists");
        Expect(grouped.Groups[0].Items.size() == 2,
            "songs should match title, artist, and album identity fields");
        Expect(grouped.Groups[1].Items.size() == 1
                && grouped.Groups[1].Items[0].Subtitle == L"Aurora · 2024"
                && grouped.Groups[1].Items[0].MemberIndices.size() == 2,
            "albums should group their members and use a trustworthy supplied year");
        Expect(grouped.Groups[2].Items.size() == 1
                && grouped.Groups[2].Items[0].Title == L"Aurora Picks",
            "playlists should match their name only, not a rendered or curator subtitle");
        Expect(grouped.Groups[3].Items.size() == 1
                && grouped.Groups[3].Items[0].Subtitle == L"1 album",
            "artist subtitles should count distinct albums rather than songs");
        Expect(grouped.DisplayedEntityCount == 5,
            "the displayed total should count entities across all groups");

        auto const subtitleFalsePositive = policy::BuildGroupedBrowseResults(inputs, L"tracks", false);
        Expect(subtitleFalsePositive.Groups.empty(),
            "rendered count subtitles must not participate in identity matching");
        auto const artistFalsePositive = policy::BuildGroupedBrowseResults(inputs, L"curator", false);
        Expect(artistFalsePositive.Groups.empty(),
            "playlist subtitles must not participate in identity matching");

        std::vector<policy::BrowseResultInput> const resolvedUrl{
            { Kind::Song, L"Resolved title", L"Resolved artist", L"Resolved album", {}, L"3:00", L"url", 0 }
        };
        auto const withoutUrlPreservation = policy::BuildGroupedBrowseResults(
            resolvedUrl, L"https://example.test/watch/123", false);
        Expect(withoutUrlPreservation.Groups.empty(),
            "resolved metadata should not accidentally match the original URL");
        auto const withUrlPreservation = policy::BuildGroupedBrowseResults(
            resolvedUrl, L"https://example.test/watch/123", true);
        Expect(withUrlPreservation.Groups.size() == 1
                && withUrlPreservation.Groups[0].Kind == Kind::Song
                && withUrlPreservation.DisplayedEntityCount == 1,
            "an HTTP lookup should preserve its resolved song result");

        Expect(policy::ParseBrowseViewPreference(L"list") == policy::BrowseViewPreference::List,
            "the persisted list preference should parse");
        Expect(policy::ParseBrowseViewPreference(L" LIST ") == policy::BrowseViewPreference::List,
            "the persisted view preference should be normalized");
        Expect(policy::ParseBrowseViewPreference(L"tiles") == policy::BrowseViewPreference::Grid,
            "an invalid view preference should safely fall back to grid");
        Expect(policy::ParseBrowseViewPreference(L"") == policy::BrowseViewPreference::Grid,
            "a missing view preference should safely fall back to grid");
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
        Expect(!policy::DownloadSchedulingAllowed(true, true, true, false, false, false),
            "the network rule should hold jobs back on a metered connection");
        Expect(policy::DownloadSchedulingAllowed(true, false, true, false, false, false),
            "an unmetered connection should run even with the network rule on: "
            "a wired desktop is not a data plan");
        Expect(policy::DownloadSchedulingAllowed(false, true, true, false, false, false),
            "turning the network rule off should allow a metered connection");
        Expect(!policy::DownloadSchedulingAllowed(false, false, false, true, false, false),
            "battery-blocked jobs should wait while unplugged");
        Expect(policy::DownloadSchedulingAllowed(false, false, true, true, false, false),
            "explicit battery permission should allow an otherwise valid job");
        Expect(!policy::DownloadSchedulingAllowed(false, true, true, false, true, false),
            "global pause should override available network and power");
    }

    void TestResumePositionPolicy()
    {
        Expect(policy::ResumePositionToStore(5.0, 300.0) == 0.0,
            "a glance at the opening should not become a resume point");
        Expect(policy::ResumePositionToStore(120.0, 300.0) == 120.0,
            "a stop in the middle should be stored as-is");
        Expect(policy::ResumePositionToStore(295.0, 300.0) == 0.0,
            "a stop in the outro should clear rather than store");
        Expect(policy::ResumePositionToStore(120.0, 0.0) == 120.0,
            "an unknown duration should still store past the head margin");

        Expect(!policy::CanResumeFrom(0.0, 300.0),
            "an unset position is not resumable");
        Expect(policy::CanResumeFrom(120.0, 300.0),
            "a stored middle position is resumable");
        Expect(!policy::CanResumeFrom(120.0, 130.0),
            "a position past a shorter replacement file is not resumable");
    }
}

int main()
{
    try
    {
        TestDetailSortPolicy();
        TestPlaylistSuggestionPolicy();
        TestRecentSearchStore();
        TestGenreArtworkRecipe();
        TestBrowseGroupingPolicy();
        TestResumePositionPolicy();
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
