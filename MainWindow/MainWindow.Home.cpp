#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/ProviderClient.h"
#include "Backend/SettingsManager.h"
#include "Backend/TrayIcon.h"
#include "Backend/DiscordPresence.h"

#include <windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Text.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    void MainWindow::RecordHomePlayback(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!IsPlayableHomeTrack(track))
        {
            return;
        }

        auto key = HomeQueueDedupeKey(track);
        if (key.empty())
        {
            return;
        }

        ++m_homePlaySequence;
        m_homePlayCounts[key] = m_homePlayCounts[key] + 1;
        m_homeLastPlayedOrder[key] = m_homePlaySequence;

        m_homeRecentHistory.erase(
            std::remove_if(m_homeRecentHistory.begin(), m_homeRecentHistory.end(), [&](auto const& item)
            {
                return HomeQueueDedupeKey(item) == key;
            }),
            m_homeRecentHistory.end());
        m_homeRecentHistory.insert(m_homeRecentHistory.begin(), track);
        if (m_homeRecentHistory.size() > 50)
        {
            m_homeRecentHistory.resize(50);
        }

        // The Home shelves are rebuilt by PlayTrack after PersistTrackPlayback has
        // refreshed the durable catalog, so a just-played track also appears in
        // Recently Added immediately (not just in Listen Again).
    }

    void MainWindow::PlayHomeMix(winrt::hstring const& mixId)
    {
        auto mixIt = m_homeMixes.find(std::wstring(mixId.c_str()));
        if (mixIt == m_homeMixes.end() || mixIt->second.empty())
        {
            return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> queue;
        queue.reserve(mixIt->second.size());
        std::unordered_set<std::wstring> queuedKeys;
        for (auto const& track : mixIt->second)
        {
            if (!IsPlayableHomeTrack(track))
            {
                continue;
            }

            auto key = HomeQueueDedupeKey(track);
            if (key.empty() || queuedKeys.find(key) != queuedKeys.end())
            {
                continue;
            }

            queuedKeys.insert(std::move(key));
            queue.push_back(track);
        }

        if (queue.empty())
        {
            return;
        }

        MusicListView().SelectedItem(queue.front());
        SetPlaybackQueue(queue, 0);
        PlayTrack(queue.front());
    }

    void MainWindow::HomeMixCard_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        (void)args;
        if (auto element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            auto mixId = ReadTagString(element.Tag());
            if (!mixId.empty())
            {
                PlayHomeMix(mixId);
            }
        }
    }

    namespace
    {
        // Mix-menu handlers route through this — the MenuFlyoutItem's Tag
        // carries the literal mix id (e.g. "daily1"), mirroring the host
        // Grid's own Tag used by HomeMixCard_Tapped.
        winrt::hstring MixIdFromMenuSender(winrt::Windows::Foundation::IInspectable const& sender)
        {
            if (auto item = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
            {
                return ReadTagString(item.Tag());
            }
            return {};
        }
    }

    void MainWindow::HomeMixMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto mixId = MixIdFromMenuSender(sender);
        if (mixId.empty())
        {
            return;
        }
        PlayHomeMix(mixId);
    }

    void MainWindow::HomeMixMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto mixId = MixIdFromMenuSender(sender);
        if (mixId.empty())
        {
            return;
        }
        auto it = m_homeMixes.find(std::wstring{ mixId.c_str() });
        if (it == m_homeMixes.end() || it->second.empty())
        {
            return;
        }
        if (!AudioPlayerService().GetCurrentTrack())
        {
            // Nothing playing → behave like Play now so the user hears
            // something instead of silently dropping items into Up Next.
            PlayHomeMix(mixId);
            return;
        }
        PlayNextFromSongsBulk(it->second);
    }

    void MainWindow::HomeMixMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto mixId = MixIdFromMenuSender(sender);
        if (mixId.empty())
        {
            return;
        }
        auto it = m_homeMixes.find(std::wstring{ mixId.c_str() });
        if (it == m_homeMixes.end() || it->second.empty())
        {
            return;
        }
        AddSongsTracksToQueueBulk(it->second);
    }

    void MainWindow::HomeMixMenuShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto mixId = MixIdFromMenuSender(sender);
        if (mixId.empty())
        {
            return;
        }
        auto mixIt = m_homeMixes.find(std::wstring(mixId.c_str()));
        if (mixIt == m_homeMixes.end() || mixIt->second.empty())
        {
            return;
        }

        // Pre-shuffle the source vector before queueing. PlayHomeMix
        // would otherwise always start at mix[0] and queue in natural
        // order; RebuildUpNextQueue walks the queue linearly, so the
        // user would *see* unshuffled tracks in the Up Next rail even
        // though auto-advance later randomises (via m_queue.ShuffleOrder).
        // Pre-shuffling makes the first played track random AND the
        // visible rail match.
        std::vector<winrt::Last_Music_Player::TrackInfo> source(mixIt->second);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(source.begin(), source.end(), gen);

        // Same dedup/filter pass as PlayHomeMix, run after the shuffle.
        std::vector<winrt::Last_Music_Player::TrackInfo> queue;
        queue.reserve(source.size());
        std::unordered_set<std::wstring> queuedKeys;
        for (auto const& track : source)
        {
            if (!IsPlayableHomeTrack(track)) continue;
            auto key = HomeQueueDedupeKey(track);
            if (key.empty() || queuedKeys.find(key) != queuedKeys.end()) continue;
            queuedKeys.insert(std::move(key));
            queue.push_back(track);
        }
        if (queue.empty())
        {
            return;
        }

        EnsureShuffleOn();
        MusicListView().SelectedItem(queue.front());
        SetPlaybackQueue(queue, 0);
        PlayTrack(queue.front());
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HomeRecentGridView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }

        MusicListView().SelectedItem(clickedTrack);

        // Home tiles play only the selected track rather than queueing the shelf.
        std::vector<winrt::Last_Music_Player::TrackInfo> homeQueue{ clickedTrack };
        SetPlaybackQueue(homeQueue, 0);
        PlayTrack(clickedTrack);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HomeRecentlyAdded_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;

        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }

        MusicListView().SelectedItem(clickedTrack);

        // Recently Added is a home carousel, not a durable collection —
        // single-track click semantics, same rationale as Listen Again
        // and search. Songs view still queues its full filtered query.
        std::vector<winrt::Last_Music_Player::TrackInfo> recentQueue{ clickedTrack };
        SetPlaybackQueue(recentQueue, 0);
        PlayTrack(clickedTrack);
    }

    void MainWindow::HomeRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto track = TrackFromActionSender(sender);
        if (!track)
        {
            return;
        }

        MusicListView().SelectedItem(track);

        // Same single-track-on-click semantics the Home carousels use (see
        // HomeRecentGridView_ItemClick): a Home card is an ephemeral tile, so
        // "Play now" queues just this track rather than the whole strip. The
        // Songs/Library "Play now" instead loads its filtered query as the
        // queue context, which would be the wrong context on Home.
        std::vector<winrt::Last_Music_Player::TrackInfo> homeQueue{ track };
        SetPlaybackQueue(homeQueue, 0);
        PlayTrack(track);
    }

    void MainWindow::HomeListenAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // "See all" on Listen Again -> full library Songs view.
        SongsButton_Click(sender, args);
        SelectSongsFilter(L"All");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HomeMostPlayed_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }
        MusicListView().SelectedItem(clickedTrack);
        // Single-track click semantics, same as the other home carousels —
        // we don't queue the whole Most Played list just because the user
        // clicked one of them.
        std::vector<winrt::Last_Music_Player::TrackInfo> mostPlayedQueue{ clickedTrack };
        SetPlaybackQueue(mostPlayedQueue, 0);
        PlayTrack(clickedTrack);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HomeLiked_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }
        MusicListView().SelectedItem(clickedTrack);
        std::vector<winrt::Last_Music_Player::TrackInfo> likedQueue{ clickedTrack };
        SetPlaybackQueue(likedQueue, 0);
        PlayTrack(clickedTrack);
    }

    void MainWindow::HomeLikedSeeAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // "See all" on Your Liked Songs -> Songs view with the Favourites
        // chip pre-selected. ChipFav uses Tag="Fav" so the filter string
        // is L"Fav" (see MainWindow.xaml:1418).
        SongsButton_Click(sender, args);
        SelectSongsFilter(L"Fav");
    }

    void MainWindow::HomeMostPlayedSeeAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // "See all" on Most Played -> Songs view with the Most played
        // chip pre-selected. ChipMost uses Tag="Most" (MainWindow.xaml:1417).
        SongsButton_Click(sender, args);
        SelectSongsFilter(L"Most");
    }

    void MainWindow::UpdateHomeGreeting(winrt::hstring const& displayName)
    {
        // Time-of-day greeting, picks a bucket by local hour. The name is
        // resolved by the caller because in Account mode it comes from the
        // signed-in profile rather than the UserDisplayName setting.
        SYSTEMTIME now{};
        GetLocalTime(&now);
        wchar_t const* greetingPrefix = L"Good evening";
        if (now.wHour >= 4 && now.wHour < 12)       greetingPrefix = L"Good morning";
        else if (now.wHour >= 12 && now.wHour < 17) greetingPrefix = L"Good afternoon";
        else if (now.wHour >= 17 && now.wHour < 22) greetingPrefix = L"Good evening";
        else                                        greetingPrefix = L"Good night";

        std::wstring greeting{ greetingPrefix };
        greeting += L", ";
        greeting += displayName;
        if (auto tb = HomeGreeting())
        {
            tb.Text(winrt::hstring{ greeting });
        }
    }

    void MainWindow::ApplyUserDisplayName()
    {
        // Pushes the resolved identity to every shell surface that shows who
        // the user is: sidebar name, sidebar avatar and plan badge, "Made for
        // X" carousel header, and the Home greeting. Which identity that is
        // (typed name or MagnetoFX profile) is decided by ChooseProfileIdentity.
        auto identity = detail::ResolveProfileIdentity();
        std::wstring name{ identity.Name.c_str() };

        if (auto tb = ProfileSidebarName())
        {
            tb.Text(identity.Name);
        }
        if (auto tb = HomeMadeForTitle())
        {
            tb.Text(winrt::hstring{ L"Made for " + name });
        }

        // The plan badge only means something for an account identity, so it
        // disappears entirely rather than showing a stale or invented tier.
        if (auto tb = ProfileSidebarPlan())
        {
            tb.Text(identity.PlanLabel);
            tb.Visibility(identity.PlanLabel.empty()
                ? Visibility::Collapsed
                : Visibility::Visible);
        }

        detail::ApplyAvatarPicture(ProfileSidebarAvatarImage(), ProfileSidebarAvatarGlyph(), identity.AvatarUrl);

        UpdateHomeGreeting(identity.Name);
    }

    void MainWindow::HomeMadeForAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        // "See all" on Made for Debashis -> Library view.
        LibraryButton_Click(sender, args);
    }


    void MainWindow::QueueStartupDataLoad(winrt::hstring savedLibraryPath)
    {
        RunDetached(HydrateStartupAsync(savedLibraryPath));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateStartupAsync(winrt::hstring savedLibraryPath)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        auto epoch = ++m_homeHydration.StartupEpoch;


        m_homeLoadState = LoadState::Loading;
        if (ListenAgainEmptyText())
        {
            ListenAgainEmptyText().Text(L"Loading your music...");
            ListenAgainEmptyText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        if (RecentlyAddedEmptyText())
        {
            RecentlyAddedEmptyText().Text(L"Loading your library...");
            RecentlyAddedEmptyText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        co_await winrt::resume_background();

        bool dbReady = DatabaseService().IsInitialized();
        AppStateSnapshot appStateSnapshot;
        bool appStateLoaded = false;
        if (!dbReady)
        {

            dbReady = DatabaseService().Initialize();

        }

        LastMusicPlayer::Backend::LibraryStats stats{};
        uint64_t maxPlayedOrder = 0;
        std::vector<winrt::Last_Music_Player::TrackInfo> sidebarPlaylists;
        std::vector<winrt::Last_Music_Player::TrackInfo> startupHistory;
        std::vector<LastMusicPlayer::Backend::PlaybackStatSnapshot> playbackStats;
        if (dbReady)
        {
            // Deactivate local tracks whose files were deleted off disk (e.g. the
            // whole music folder was removed) before reading any stats/pages, so
            // every IsActive-filtered surface below reflects the pruned library.

            PruneMissingLocalTracks();

            stats = DatabaseService().GetLibraryStats();
            maxPlayedOrder = DatabaseService().MaxLastPlayedOrder();
            sidebarPlaylists = DatabaseService().LoadRecentPlaylists(4);
            startupHistory = DatabaseService().LoadHistoryTracks(true, true);
            playbackStats = DatabaseService().LoadPlaybackStats();

        }

        try
        {

            auto jsonPayload = ReadTextFile(StateFilePath());
            if (!jsonPayload.empty())
            {
                appStateSnapshot = MainWindow::ParseAppStateSnapshot(jsonPayload);
                appStateLoaded = true;
            }

        }
        catch (...)
        {

        }

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_homeHydration.StartupEpoch)
        {
            co_return;
        }

        m_catalogLoaded = dbReady;
        m_libraryStats = stats;
        m_homePlaySequence = (std::max)(m_homePlaySequence, maxPlayedOrder);
        m_homeRecentHistory = std::move(startupHistory);
        m_homePlayCounts.clear();
        m_homeLastPlayedOrder.clear();
        for (auto const& playback : playbackStats)
        {
            auto key = playback.Track ? HomeQueueDedupeKey(playback.Track) : std::wstring{};
            if (key.empty())
            {
                continue;
            }
            m_homePlayCounts[key] = playback.PlayCount;
            m_homeLastPlayedOrder[key] = playback.LastPlayedOrder;
            m_homePlaySequence = (std::max)(m_homePlaySequence, playback.LastPlayedOrder);
        }
        MarkLibraryViewsDirty();
        m_sidebarPlaylists.Clear();
        for (auto const& playlist : sidebarPlaylists)
        {
            auto copy = playlist;
            ResolveArtworkPresentation(copy, L"playlist");
            m_sidebarPlaylists.Append(copy);
        }
        UpdateSongsScopeLabel();



        if (appStateLoaded)
        {
            ApplyAppStateSnapshot(appStateSnapshot);
        }
        SaveAppState();

        co_await HydrateHomeAsync(false);


    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateHomeAsync(bool refreshProvider)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();

        // Single-flight: the home view was being re-hydrated up to a dozen
        // times per startup, each pass = 4 DB queries + a 240-row pull +
        // a full collection rebuild. If a hydrate is already running, just
        // record that another pass is wanted and let the in-flight one kick
        // off exactly one more at the end (latest DB state still wins via the
        // epoch guard). Runs on the UI thread before any co_await, so the
        // flag check/set is race-free.
        if (m_homeHydration.InFlight)
        {
            m_homeHydration.Pending = true;
            m_homeHydration.PendingRefresh = m_homeHydration.PendingRefresh || refreshProvider;
            co_return;
        }
        m_homeHydration.InFlight = true;

        auto epoch = ++m_homeHydration.HomeEpoch;
        m_homeLoadState = LoadState::Loading;


        co_await winrt::resume_background();

        // Abort a superseded run *before* paying for the DB work, not only
        // after it (the old code only checked post-query).
        if (epoch != m_homeHydration.HomeEpoch)
        {
            co_await wil::resume_foreground(dispatcher);
            FinishHomeHydration();
            co_return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> history;
        std::vector<winrt::Last_Music_Player::TrackInfo> recentlyAdded;
        std::vector<winrt::Last_Music_Player::TrackInfo> mostPlayed;
        std::vector<winrt::Last_Music_Player::TrackInfo> liked;
        std::vector<winrt::Last_Music_Player::TrackInfo> mixSeed;
        std::vector<std::wstring> rankedGenres;
        std::unordered_map<std::wstring, std::vector<winrt::Last_Music_Player::TrackInfo>> genrePools;
        LastMusicPlayer::Backend::LibraryStats stats{};
        if (DatabaseService().IsInitialized())
        {
            LastMusicPlayer::Backend::TrackQuery historyQuery;
            historyQuery.Filter = L"History";
            historyQuery.Limit = 12;
            history = DatabaseService().LoadTrackPage(historyQuery).Tracks;

            recentlyAdded = DatabaseService().LoadRecentlyAddedTracks(6);

            // Top tracks by PlayCount — drives the Most Played carousel.
            // LoadMostPlayedTracks returns the full sorted set; the C++
            // population step below caps to 10.
            mostPlayed = DatabaseService().LoadMostPlayedTracks(true, true);

            // User-liked tracks — drives the Liked Songs Highlights carousel.
            // Reuses the same "Liked" filter the Songs view exposes.
            LastMusicPlayer::Backend::TrackQuery likedQuery;
            likedQuery.Filter = L"Liked";
            likedQuery.Sort = L"DateAdded";
            likedQuery.Limit = 12;
            liked = DatabaseService().LoadTrackPage(likedQuery).Tracks;

            LastMusicPlayer::Backend::TrackQuery seedQuery;
            seedQuery.Sort = L"DateAdded";
            seedQuery.Limit = 240;
            mixSeed = DatabaseService().LoadTrackPage(seedQuery).Tracks;
            stats = DatabaseService().GetLibraryStats();

            // Genre-clustered Daily Mixes: pull the user's top genres (by
            // listening) and the full track set for each. Done here on the
            // background thread — LoadTracksForGroup escapes the 240-row
            // mixSeed cap so a genre mix can include older favourites too.
            // Three drive Daily Mix 1/2/3; a spare gives BuildHomeMixes room
            // to skip any genre that fully overlaps an earlier mix.
            rankedGenres = DatabaseService().LoadTopGenres(6);
            for (auto const& genre : rankedGenres)
            {
                // Load up to 6 pools so the four genre-clustered Daily Mixes
                // (1-4) can each claim a distinct non-empty genre even when a
                // couple of higher-ranked genres come back empty.
                if (genrePools.size() >= 6)
                {
                    break;
                }
                genrePools[genre] = DatabaseService().LoadTracksForGroup(L"genre", genre);
            }
        }

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_homeHydration.HomeEpoch)
        {
            FinishHomeHydration();
            co_return;
        }

        if (DatabaseService().IsInitialized())
        {
            m_catalogTracks = mixSeed;
            m_libraryStats = stats;
            m_homeRankedGenres = std::move(rankedGenres);
            m_homeGenrePools.clear();
            for (auto& [genre, tracks] : genrePools)
            {
                for (auto& track : tracks)
                {
                    ResolveArtworkPresentation(track, L"track");
                }
                m_homeGenrePools.emplace(genre, std::move(tracks));
            }
        }
        else if (m_catalogTracks.empty())
        {
            m_catalogTracks.assign(m_queue.CurrentPlaylist.begin(), m_queue.CurrentPlaylist.end());
        }

        m_homeTracks.Clear();
        std::unordered_set<std::wstring> listenKeys;
        auto appendListenTrack = [&](winrt::Last_Music_Player::TrackInfo const& candidate)
        {
            if (m_homeTracks.Size() >= 12 || !candidate || !IsPlayableHomeTrack(candidate))
            {
                return;
            }
            auto key = HomeQueueDedupeKey(candidate);
            if (key.empty() || listenKeys.find(key) != listenKeys.end())
            {
                return;
            }
            auto copy = candidate;
            ResolveArtworkPresentation(copy, L"track");
            listenKeys.insert(std::move(key));
            m_homeTracks.Append(copy);
        };
        for (auto const& track : m_homeRecentHistory)
        {
            appendListenTrack(track);
        }
        for (auto const& track : history)
        {
            appendListenTrack(track);
        }

        m_recentlyAddedTracks.Clear();
        bool anyMeaningfulRecentlyAdded = false;
        for (auto const& track : recentlyAdded)
        {
            auto copy = track;
            ResolveArtworkPresentation(copy, L"track");
            m_recentlyAddedTracks.Append(copy);
            // DateAddedSortKey is only set for genuinely-added library
            // entries (local scans, imported playlists). Search-result
            // remote tracks land with key=0; surface the section only
            // when at least one real library addition exists.
            if (track.DateAddedSortKey() > 0.0)
            {
                anyMeaningfulRecentlyAdded = true;
            }
        }

        m_homeMostPlayedTracks.Clear();
        {
            // Cap to 10 to keep the carousel reasonable; the helper
            // returns the whole sorted set.
            size_t taken = 0;
            for (auto const& track : mostPlayed)
            {
                if (taken >= 10) break;
                auto copy = track;
                ResolveArtworkPresentation(copy, L"track");
                m_homeMostPlayedTracks.Append(copy);
                ++taken;
            }
        }

        m_homeLikedTracks.Clear();
        for (auto const& track : liked)
        {
            auto copy = track;
            ResolveArtworkPresentation(copy, L"track");
            m_homeLikedTracks.Append(copy);
        }


        BuildHomeMixes();


        using winrt::Microsoft::UI::Xaml::Visibility;
        bool hasListenAgain = m_homeTracks.Size() > 0;
        bool hasRecentlyAdded = m_recentlyAddedTracks.Size() > 0;
        HomeRecentGridView().Visibility(hasListenAgain ? Visibility::Visible : Visibility::Collapsed);
        ListenAgainEmptyText().Text(hasListenAgain ? L"" : L"Play something to see it here");
        ListenAgainEmptyText().Visibility(hasListenAgain ? Visibility::Collapsed : Visibility::Visible);
        // Section-level collapse: if no real library additions exist, the
        // whole "Recently Added" block disappears (header + carousel +
        // empty text). The inner GridView / empty-text toggles below
        // still run for completeness but they're invisible inside the
        // collapsed parent.
        HomeRecentlyAddedSection().Visibility(anyMeaningfulRecentlyAdded ? Visibility::Visible : Visibility::Collapsed);
        HomeRecentlyAddedGridView().Visibility(hasRecentlyAdded ? Visibility::Visible : Visibility::Collapsed);
        RecentlyAddedEmptyText().Text(hasRecentlyAdded ? L"" : L"Play something to see it here");
        RecentlyAddedEmptyText().Visibility(hasRecentlyAdded ? Visibility::Collapsed : Visibility::Visible);

        // Hide the Most Played section until the user has enough play
        // history to make it interesting — on first launch the "top
        // tracks" set is meaningless. Three is the threshold (matches
        // the plan's "< 3 tracks => hide" decision).
        bool hasMostPlayed = m_homeMostPlayedTracks.Size() >= 3;
        HomeMostPlayedSection().Visibility(hasMostPlayed ? Visibility::Visible : Visibility::Collapsed);

        // Liked section appears the moment the user likes any track.
        bool hasLiked = m_homeLikedTracks.Size() > 0;
        HomeLikedSection().Visibility(hasLiked ? Visibility::Visible : Visibility::Collapsed);

        // Refresh display-name surfaces (cheap; safe to call every hydrate).
        // Covers avatar initial, sidebar name, "Made for X" header, and the
        // time-aware greeting in one call.
        ApplyUserDisplayName();
        m_homeLoadState = LoadState::Loaded;


        auto weakThis = get_weak();
        auto autoPlaylistEpoch = epoch;

        dispatcher.TryEnqueue([weakThis, autoPlaylistEpoch]()
        {
            if (auto self = weakThis.get())
            {
                if (autoPlaylistEpoch != self->m_homeHydration.HomeEpoch)
                {
                    return;
                }

                self->RefreshAutoPlaylists();

            }
        });

        if (refreshProvider)
        {
            auto refreshId = ++m_homeHydration.MixRefreshId;

            dispatcher.TryEnqueue([weakThis, refreshId]()
            {
                if (auto self = weakThis.get())
                {
                    self->RefreshHomeProviderMixesAsync(refreshId);
                }
            });
        }

        FinishHomeHydration();
    }

    void MainWindow::FinishHomeHydration()
    {
        // Clear the in-flight latch; if requests arrived while we were
        // running, collapse them into exactly one more hydrate that will
        // observe the latest DB state.
        m_homeHydration.InFlight = false;
        if (m_homeHydration.Pending)
        {
            m_homeHydration.Pending = false;
            bool wantRefresh = m_homeHydration.PendingRefresh;
            m_homeHydration.PendingRefresh = false;
            RunDetached(HydrateHomeAsync(wantRefresh));
        }
    }


    void MainWindow::PopulateHomeFromLibrary()
    {
        m_homeTracks.Clear();

        auto resolveTrack = [this](winrt::Last_Music_Player::TrackInfo const& source)
        {
            if (source.File() || IsHttpUrl(source.FilePath()))
            {
                return source;
            }

            auto path = source.FilePath();
            if (!path.empty())
            {
                for (auto const& localTrack : m_queue.CurrentPlaylist)
                {
                    if (localTrack.FilePath() == path)
                    {
                        return localTrack;
                    }
                }
            }

            return source;
        };

        std::unordered_set<std::wstring> listenKeys;
        auto appendListenTrack = [&](winrt::Last_Music_Player::TrackInfo const& candidate)
        {
            if (m_homeTracks.Size() >= 12)
            {
                return;
            }

            auto track = resolveTrack(candidate);
            if (!IsPlayableHomeTrack(track))
            {
                return;
            }

            auto key = HomeQueueDedupeKey(track);
            if (key.empty() || listenKeys.find(key) != listenKeys.end())
            {
                return;
            }

            listenKeys.insert(std::move(key));
            m_homeTracks.Append(track);
        };

        // Listen Again = the durable play history. In-session history first (so a
        // just-played track shows immediately), then the SQLite history which
        // survives both restarts and reinstalls (it lives in %LOCALAPPDATA%).
        for (auto const& track : m_homeRecentHistory)
        {
            appendListenTrack(track);
        }
        if (DatabaseService().IsInitialized())
        {
            for (auto const& track : DatabaseService().LoadHistoryTracks(true, true))
            {
                appendListenTrack(track);
            }
        }

        HomeRecentGridView().ItemsSource(m_homeTracks);
        HomeResultsTitle().Text(L"Listen Again");
        HomeResultsBadgeText().Text(L"History");

        // Recently Added = the durable catalog (every track ever played, imported,
        // or scanned - remote and local), newest-added first. m_catalogTracks is
        // loaded from the SQLite catalog and already survives restart/reinstall.
        m_recentlyAddedTracks.Clear();
        std::vector<winrt::Last_Music_Player::TrackInfo> recentlyAdded{ m_catalogTracks.begin(), m_catalogTracks.end() };
        std::sort(recentlyAdded.begin(), recentlyAdded.end(), [](auto const& left, auto const& right)
        {
            if (left.DateAddedSortKey() != right.DateAddedSortKey())
            {
                return left.DateAddedSortKey() > right.DateAddedSortKey();
            }
            return left.Index() > right.Index();
        });
        std::unordered_set<std::wstring> recentlyAddedKeys;
        for (auto const& candidate : recentlyAdded)
        {
            if (m_recentlyAddedTracks.Size() >= 6)
            {
                break;
            }

            auto track = resolveTrack(candidate);
            if (!IsPlayableHomeTrack(track))
            {
                continue;
            }

            auto key = HomeQueueDedupeKey(track);
            if (key.empty() || recentlyAddedKeys.find(key) != recentlyAddedKeys.end())
            {
                continue;
            }

            recentlyAddedKeys.insert(std::move(key));
            m_recentlyAddedTracks.Append(track);
        }
        HomeRecentlyAddedGridView().ItemsSource(m_recentlyAddedTracks);

        BuildHomeMixes();
        RefreshAutoPlaylists();

        // Show real, durable data when it exists; otherwise show a subtle
        // "play something" hint instead of placeholder cards.
        using winrt::Microsoft::UI::Xaml::Visibility;
        bool hasListenAgain = m_homeTracks.Size() > 0;
        bool hasRecentlyAdded = m_recentlyAddedTracks.Size() > 0;
        HomeRecentGridView().Visibility(hasListenAgain ? Visibility::Visible : Visibility::Collapsed);
        ListenAgainEmptyText().Visibility(hasListenAgain ? Visibility::Collapsed : Visibility::Visible);
        HomeRecentlyAddedGridView().Visibility(hasRecentlyAdded ? Visibility::Visible : Visibility::Collapsed);
        RecentlyAddedEmptyText().Visibility(hasRecentlyAdded ? Visibility::Collapsed : Visibility::Visible);
    }

    void MainWindow::BuildHomeMixes()
    {
        auto smartLiked = std::move(m_homeMixes[L"smart-liked"]);
        auto smartMostPlayed = std::move(m_homeMixes[L"smart-most"]);
        auto smartRecentlyAdded = std::move(m_homeMixes[L"smart-recent"]);
        m_homeMixes.clear();
        m_homeMixes[L"smart-liked"] = std::move(smartLiked);
        m_homeMixes[L"smart-most"] = std::move(smartMostPlayed);
        m_homeMixes[L"smart-recent"] = std::move(smartRecentlyAdded);

        auto resolveTrack = [this](winrt::Last_Music_Player::TrackInfo const& source)
        {
            if (source.File() || IsHttpUrl(source.FilePath()))
            {
                return source;
            }

            auto path = source.FilePath();
            if (!path.empty())
            {
                for (auto const& localTrack : m_queue.CurrentPlaylist)
                {
                    if (localTrack.FilePath() == path)
                    {
                        return localTrack;
                    }
                }
            }

            return source;
        };

        auto appendUnique = [&](std::vector<winrt::Last_Music_Player::TrackInfo>& mix, winrt::Last_Music_Player::TrackInfo const& candidate, size_t limit)
        {
            if (mix.size() >= limit)
            {
                return false;
            }

            auto track = resolveTrack(candidate);
            if (!IsPlayableHomeTrack(track))
            {
                return false;
            }

            auto key = HomeQueueDedupeKey(track);
            if (key.empty())
            {
                return false;
            }

            for (auto const& existing : mix)
            {
                if (HomeQueueDedupeKey(existing) == key)
                {
                    return false;
                }
            }

            mix.push_back(track);
            return true;
        };

        auto appendAll = [&](std::vector<winrt::Last_Music_Player::TrackInfo>& mix, auto const& tracks, size_t limit)
        {
            for (auto const& track : tracks)
            {
                appendUnique(mix, track, limit);
                if (mix.size() >= limit)
                {
                    break;
                }
            }
        };

        auto appendByArtist = [&](std::vector<winrt::Last_Music_Player::TrackInfo>& mix, std::wstring const& artist, size_t limit)
        {
            auto wanted = CanonicalQueueText(winrt::hstring(artist));
            if (wanted.empty())
            {
                return;
            }

            for (auto const& track : m_catalogTracks)
            {
                if (CanonicalQueueText(track.Artist()) == wanted)
                {
                    appendUnique(mix, track, limit);
                    if (mix.size() >= limit)
                    {
                        break;
                    }
                }
            }
        };

        // "Made for <user>" Daily Mixes are genre-clustered: each pulls from one
        // of the user's top genres (m_homeRankedGenres, play-weighted, loaded in
        // HydrateHomeAsync) so the three mixes are genuinely distinct rather than
        // three slices of the same recently-added catalog tail. Genre pools come
        // straight from the DB (LoadTracksForGroup), so they aren't bounded by the
        // 240-row mixSeed. Each genre mix is topped up with the catalog so it's
        // never short. When the library has too few tagged genres, a slot falls
        // back to the prior artist/catalog logic and leaves m_homeMixGenres empty
        // so the card keeps its generic label.
        auto topArtists = RankedHomeArtists(m_catalogTracks, m_homePlayCounts);
        constexpr size_t mixLimit = 12;
        m_homeMixGenres.clear();

        // Hands out each ranked genre to at most one daily slot (distinct mixes),
        // skipping any genre whose pool came back empty.
        size_t genreCursor = 0;
        auto nextGenre = [&]() -> std::wstring
        {
            while (genreCursor < m_homeRankedGenres.size())
            {
                auto const& genre = m_homeRankedGenres[genreCursor++];
                auto poolIt = m_homeGenrePools.find(genre);
                if (poolIt != m_homeGenrePools.end() && !poolIt->second.empty())
                {
                    return genre;
                }
            }
            return std::wstring{};
        };

        // Daily Mix 1 — top genre, else recent history + #1 artist.
        std::vector<winrt::Last_Music_Player::TrackInfo> daily1;
        if (auto genre = nextGenre(); !genre.empty())
        {
            appendAll(daily1, m_homeGenrePools[genre], mixLimit);
            m_homeMixGenres[L"daily1"] = genre;
        }
        else
        {
            appendAll(daily1, m_homeRecentHistory, mixLimit);
            if (!topArtists.empty())
            {
                appendByArtist(daily1, topArtists[0], mixLimit);
            }
        }
        appendAll(daily1, m_catalogTracks, mixLimit);
        m_homeMixes[L"daily1"] = daily1;

        // Daily Mix 2 — next top genre, else #2 artist + catalog stride.
        std::vector<winrt::Last_Music_Player::TrackInfo> daily2;
        if (auto genre = nextGenre(); !genre.empty())
        {
            appendAll(daily2, m_homeGenrePools[genre], mixLimit);
            m_homeMixGenres[L"daily2"] = genre;
        }
        else
        {
            if (topArtists.size() > 1)
            {
                appendByArtist(daily2, topArtists[1], mixLimit);
            }
            for (size_t i = 1; i < m_catalogTracks.size(); i += 3)
            {
                appendUnique(daily2, m_catalogTracks[i], mixLimit);
            }
        }
        appendAll(daily2, m_catalogTracks, mixLimit);
        m_homeMixes[L"daily2"] = daily2;

        // Daily Mix 3 — next top genre, else artist round-robin (max 2 each).
        std::vector<winrt::Last_Music_Player::TrackInfo> daily3;
        if (auto genre = nextGenre(); !genre.empty())
        {
            appendAll(daily3, m_homeGenrePools[genre], mixLimit);
            m_homeMixGenres[L"daily3"] = genre;
        }
        else
        {
            std::unordered_map<std::wstring, uint32_t> artistBuckets;
            for (auto const& track : m_catalogTracks)
            {
                auto artist = CanonicalQueueText(track.Artist());
                auto& count = artistBuckets[artist];
                if (count >= 2)
                {
                    continue;
                }
                if (appendUnique(daily3, track, mixLimit))
                {
                    ++count;
                }
            }
        }
        appendAll(daily3, m_catalogTracks, mixLimit);
        m_homeMixes[L"daily3"] = daily3;

        // Daily Mix 4 — next top genre, else #4 artist + catalog.
        std::vector<winrt::Last_Music_Player::TrackInfo> daily4;
        if (auto genre = nextGenre(); !genre.empty())
        {
            appendAll(daily4, m_homeGenrePools[genre], mixLimit);
            m_homeMixGenres[L"daily4"] = genre;
        }
        else if (topArtists.size() > 3)
        {
            appendByArtist(daily4, topArtists[3], mixLimit);
        }
        appendAll(daily4, m_catalogTracks, mixLimit);
        m_homeMixes[L"daily4"] = daily4;

        // Daily Mix 5 — next top genre, else #5 artist + catalog.
        std::vector<winrt::Last_Music_Player::TrackInfo> daily5;
        if (auto genre = nextGenre(); !genre.empty())
        {
            appendAll(daily5, m_homeGenrePools[genre], mixLimit);
            m_homeMixGenres[L"daily5"] = genre;
        }
        else if (topArtists.size() > 4)
        {
            appendByArtist(daily5, topArtists[4], mixLimit);
        }
        appendAll(daily5, m_catalogTracks, mixLimit);
        m_homeMixes[L"daily5"] = daily5;

        std::vector<winrt::Last_Music_Player::TrackInfo> onRepeat{ m_catalogTracks.begin(), m_catalogTracks.end() };
        std::sort(onRepeat.begin(), onRepeat.end(), [&](auto const& left, auto const& right)
        {
            auto leftKey = HomeQueueDedupeKey(left);
            auto rightKey = HomeQueueDedupeKey(right);
            auto leftPlayIt = m_homePlayCounts.find(leftKey);
            auto rightPlayIt = m_homePlayCounts.find(rightKey);
            auto leftPlays = leftPlayIt == m_homePlayCounts.end() ? 0u : leftPlayIt->second;
            auto rightPlays = rightPlayIt == m_homePlayCounts.end() ? 0u : rightPlayIt->second;
            if (leftPlays != rightPlays)
            {
                return leftPlays > rightPlays;
            }
            auto leftOrderIt = m_homeLastPlayedOrder.find(leftKey);
            auto rightOrderIt = m_homeLastPlayedOrder.find(rightKey);
            auto leftOrder = leftOrderIt == m_homeLastPlayedOrder.end() ? 0ull : leftOrderIt->second;
            auto rightOrder = rightOrderIt == m_homeLastPlayedOrder.end() ? 0ull : rightOrderIt->second;
            return leftOrder > rightOrder;
        });
        std::vector<winrt::Last_Music_Player::TrackInfo> repeatMix;
        appendAll(repeatMix, onRepeat, mixLimit);
        appendAll(repeatMix, m_homeRecentHistory, mixLimit);
        appendAll(repeatMix, m_catalogTracks, mixLimit);
        m_homeMixes[L"repeat"] = repeatMix;

        // Discover surfaces the less-familiar corners of the library: tracks
        // whose genre is NOT one of the user's top genres come first, then
        // least-played within that, then newest. Falls back to pure
        // least-played ordering when no genres are ranked.
        std::unordered_set<std::wstring> topGenreSet;
        for (auto const& genre : m_homeRankedGenres)
        {
            topGenreSet.insert(ToLowerCopy(winrt::hstring(genre)));
        }
        auto isFamiliarGenre = [&](winrt::Last_Music_Player::TrackInfo const& track)
        {
            auto genre = ToLowerCopy(track.Genre());
            return !genre.empty() && topGenreSet.find(genre) != topGenreSet.end();
        };

        std::vector<winrt::Last_Music_Player::TrackInfo> leastPlayed{ m_catalogTracks.begin(), m_catalogTracks.end() };
        std::sort(leastPlayed.begin(), leastPlayed.end(), [&](auto const& left, auto const& right)
        {
            bool leftFamiliar = isFamiliarGenre(left);
            bool rightFamiliar = isFamiliarGenre(right);
            if (leftFamiliar != rightFamiliar)
            {
                return !leftFamiliar; // unfamiliar genres first
            }
            auto leftKey = HomeQueueDedupeKey(left);
            auto rightKey = HomeQueueDedupeKey(right);
            auto leftPlayIt = m_homePlayCounts.find(leftKey);
            auto rightPlayIt = m_homePlayCounts.find(rightKey);
            auto leftPlays = leftPlayIt == m_homePlayCounts.end() ? 0u : leftPlayIt->second;
            auto rightPlays = rightPlayIt == m_homePlayCounts.end() ? 0u : rightPlayIt->second;
            if (leftPlays != rightPlays)
            {
                return leftPlays < rightPlays;
            }
            return left.DateAddedSortKey() > right.DateAddedSortKey();
        });
        std::vector<winrt::Last_Music_Player::TrackInfo> discoverMix;
        appendAll(discoverMix, leastPlayed, mixLimit);
        m_homeMixes[L"discover"] = discoverMix;

        // Time Capsule — the user's oldest library additions (a nostalgia trip).
        // Real DateAdded entries (key > 0) come before undated/remote (key <= 0)
        // so the mix is genuinely "oldest first" rather than led by key-0 tracks.
        std::vector<winrt::Last_Music_Player::TrackInfo> oldest{ m_catalogTracks.begin(), m_catalogTracks.end() };
        std::sort(oldest.begin(), oldest.end(), [](auto const& left, auto const& right)
        {
            auto lk = left.DateAddedSortKey();
            auto rk = right.DateAddedSortKey();
            bool lz = lk <= 0.0;
            bool rz = rk <= 0.0;
            if (lz != rz)
            {
                return !lz; // dated tracks before undated
            }
            return lk < rk; // older first
        });
        std::vector<winrt::Last_Music_Player::TrackInfo> timeCapsuleMix;
        appendAll(timeCapsuleMix, oldest, mixLimit);
        m_homeMixes[L"timecapsule"] = timeCapsuleMix;

        // Fresh Finds — newest additions. m_catalogTracks is already
        // newest-first (LoadTrackPage Sort=DateAdded), so take it in order.
        std::vector<winrt::Last_Music_Player::TrackInfo> freshMix;
        appendAll(freshMix, m_catalogTracks, mixLimit);
        m_homeMixes[L"fresh"] = freshMix;

        // Refresh the Daily Mix card titles/subtitles to match the genres just
        // assigned (or restore generic copy where a slot fell back).
        UpdateMadeForCardLabels();

        // Provider-backed mixes are refreshed explicitly after local Home content
        // is visible, not as part of every local mix rebuild.
    }

    void MainWindow::UpdateMadeForCardLabels()
    {
        // Each Daily Mix card's description + caption reflect the genre that
        // actually drives it (set in BuildHomeMixes). When a slot fell back to
        // the artist/catalog logic (no genre), the original genre-themed copy is
        // restored so the card never shows a stale genre from a prior hydrate.
        struct CardSlot
        {
            wchar_t const* mixId;
            winrt::Microsoft::UI::Xaml::Controls::TextBlock desc;
            winrt::Microsoft::UI::Xaml::Controls::TextBlock caption;
            wchar_t const* fallbackDesc;
        };

        CardSlot slots[] = {
            { L"daily1", HomeMix1Desc(), HomeMix1Caption(), L"Upbeat indie & pop you keep coming back to." },
            { L"daily2", HomeMix2Desc(), HomeMix2Caption(), L"Mellow electronic and chill beats." },
            { L"daily3", HomeMix3Desc(), HomeMix3Caption(), L"Rock and alternative favourites." },
            { L"daily4", HomeMix4Desc(), HomeMix4Caption(), L"More from a genre you love." },
            { L"daily5", HomeMix5Desc(), HomeMix5Caption(), L"More from a genre you love." },
        };

        // Top 2 artists of a track set, play-weighted, comma-joined, with a
        // "& more" suffix when the mix spans more artists. Two names keep the
        // 184px card from wrapping into a cramped multi-line block.
        auto joinTopArtists = [&](std::vector<winrt::Last_Music_Player::TrackInfo> const& pool)
        {
            std::wstring line;
            auto artists = RankedHomeArtists(pool, m_homePlayCounts);
            for (size_t i = 0; i < artists.size() && i < 2; ++i)
            {
                if (!line.empty())
                {
                    line += L", ";
                }
                line += artists[i];
            }
            if (artists.size() > 2 && !line.empty())
            {
                line += L" & more";
            }
            return line;
        };

        for (auto const& slot : slots)
        {
            auto genreIt = m_homeMixGenres.find(slot.mixId);
            bool hasGenre = genreIt != m_homeMixGenres.end() && !genreIt->second.empty();

            // The subtitle is ALWAYS the mix's top artists (dynamic), so the
            // cards stay informative even when genre tags are missing/poor.
            // Prefer the genre pool when clustered so the names reflect the
            // genre itself rather than the catalog tracks that padded a short
            // tail; otherwise use the assembled mix.
            std::wstring artistLine;
            if (hasGenre)
            {
                auto poolIt = m_homeGenrePools.find(genreIt->second);
                if (poolIt != m_homeGenrePools.end())
                {
                    artistLine = joinTopArtists(poolIt->second);
                }
            }
            if (artistLine.empty())
            {
                auto mixIt = m_homeMixes.find(slot.mixId);
                if (mixIt != m_homeMixes.end())
                {
                    artistLine = joinTopArtists(mixIt->second);
                }
            }

            if (slot.desc)
            {
                // Only fall back to the static theme copy if we somehow have no
                // artists at all (empty mix).
                slot.desc.Text(winrt::hstring(artistLine.empty() ? slot.fallbackDesc : artistLine));
            }
            if (slot.caption)
            {
                // Genre caption only when it's a real genre; otherwise generic.
                slot.caption.Text(hasGenre ? winrt::hstring(genreIt->second) : winrt::hstring(L"Made for you"));
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RefreshHomeProviderMixesAsync(uint64_t refreshId)
    {
        auto lifetime = get_strong();
        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        if (remoteScope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
            || !remoteMusic.IsModeAvailable(remoteScope.Mode)
            || !remoteMusic.IsCurrent(remoteScope))
        {
            co_return;
        }
        auto cacheScope = LastMusicPlayer::Backend::RemoteScopeCacheKey(remoteScope);

        auto topArtists = RankedHomeArtists(m_queue.CurrentPlaylist, m_homePlayCounts);
        auto artistOr = [&](size_t index, wchar_t const* fallback)
        {
            return topArtists.size() > index && !topArtists[index].empty()
                ? winrt::hstring(topArtists[index])
                : winrt::hstring(fallback);
        };

        // When a Daily Mix is genre-clustered, fetch more of that genre online so
        // the provider fill stays on-theme; otherwise keep the artist/topic query.
        auto genreOr = [&](wchar_t const* mixId, winrt::hstring const& fallback)
        {
            auto it = m_homeMixGenres.find(mixId);
            return it != m_homeMixGenres.end() && !it->second.empty()
                ? winrt::hstring(it->second + L" songs")
                : fallback;
        };

        struct FillRequest
        {
            wchar_t const* id;
            winrt::hstring query;
            bool prepend;
        };

        std::array<FillRequest, 7> requests{ {
            { L"daily1", genreOr(L"daily1", artistOr(0, L"popular songs")), false },
            { L"daily2", genreOr(L"daily2", artistOr(1, L"chill songs")), false },
            { L"daily3", genreOr(L"daily3", artistOr(2, L"rock alternative songs")), false },
            { L"daily4", genreOr(L"daily4", artistOr(3, L"top hits")), false },
            { L"daily5", genreOr(L"daily5", artistOr(4, L"feel good songs")), false },
            { L"repeat", artistOr(0, L"popular songs"), false },
            { L"discover", topArtists.empty() ? winrt::hstring(L"new music") : winrt::hstring(topArtists[0] + L" new songs"), true },
        } };

        for (auto const& request : requests)
        {
            if (refreshId != m_homeHydration.MixRefreshId)
            {
                co_return;
            }

            try
            {
                auto cacheKey = cacheScope + L"\nhome\n" + std::wstring(request.query.c_str());
                std::vector<winrt::Last_Music_Player::TrackInfo> remoteTracks;
                auto cached = m_remoteSearchCache.find(cacheKey);
                if (cached != m_remoteSearchCache.end())
                {
                    if (!remoteMusic.IsCurrent(remoteScope))
                    {
                        co_return;
                    }
                    remoteTracks = cached->second;
                }
                else
                {
                    auto payload = co_await remoteMusic.SearchAsync(remoteScope, request.query);
                    if (refreshId != m_homeHydration.MixRefreshId
                        || !remoteMusic.IsCurrent(remoteScope))
                    {
                        co_return;
                    }
                    remoteTracks = ParseProviderTracks(payload, 8);
                    if (m_remoteSearchCache.size() >= kRemoteSearchCacheLimit)
                    {
                        m_remoteSearchCache.clear();
                    }
                    m_remoteSearchCache[cacheKey] = remoteTracks;
                }

                if (!remoteMusic.IsCurrent(remoteScope))
                {
                    co_return;
                }
                auto& mix = m_homeMixes[request.id];
                std::vector<winrt::Last_Music_Player::TrackInfo> merged;
                merged.reserve(mix.size() + remoteTracks.size());
                std::unordered_set<std::wstring> keys;

                auto append = [&](winrt::Last_Music_Player::TrackInfo const& track)
                {
                    if (!IsPlayableHomeTrack(track) || merged.size() >= 12)
                    {
                        return;
                    }

                    auto key = HomeQueueDedupeKey(track);
                    if (key.empty() || keys.find(key) != keys.end())
                    {
                        return;
                    }

                    keys.insert(std::move(key));
                    merged.push_back(track);
                };

                if (request.prepend)
                {
                    for (auto const& track : remoteTracks) append(track);
                    for (auto const& track : mix) append(track);
                }
                else
                {
                    for (auto const& track : mix) append(track);
                    for (auto const& track : remoteTracks) append(track);
                }

                if (!remoteMusic.IsCurrent(remoteScope))
                {
                    co_return;
                }
                mix = std::move(merged);
            }
            catch (winrt::hresult_canceled const&)
            {
                co_return;
            }
            catch (...)
            {
                continue;
            }
        }

        if (refreshId == m_homeHydration.MixRefreshId
            && remoteMusic.IsCurrent(remoteScope))
        {
            RefreshAutoPlaylists();
        }
    }

}
