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

    winrt::Windows::Foundation::IAsyncAction MainWindow::MusicListView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }

        MusicListView().SelectedItem(clickedTrack);
        co_await LoadSongsQueueAndPlayAsync(clickedTrack);
        co_return;
    }

    // ---- Shared queue navigation (shuffle + repeat aware) -------------------

    // ---- Transport routing (Local MediaPlayer vs Chromecast) ---------------

    // ---- Full-screen Now Playing view --------------------------------------

    // ---- Chromecast --------------------------------------------------------


    void MainWindow::AppendTrackPage(
        std::vector<winrt::Last_Music_Player::TrackInfo> const& source,
        winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> target,
        uint32_t pageSize)
    {
        if (!target || pageSize == 0)
        {
            return;
        }

        auto start = target.Size();
        auto total = static_cast<uint32_t>((std::min)(source.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        auto end = (std::min)(total, start + pageSize);
        for (uint32_t i = start; i < end; ++i)
        {
            auto copy = source[i];
            copy.Index(static_cast<int32_t>(i + 1));
            ResolveArtworkPresentation(copy, L"track");
            target.Append(copy);
        }
    }

    void MainWindow::AppendSongsPage()
    {
        AppendTrackPage(
            m_songsAllResults,
            m_songsTracks,
            m_songsGridMode ? kSongsGridPageSize : kSongsListPageSize);
        UpdateSongsStats();
    }

    void MainWindow::MaybeAppendSongsPage(uint32_t itemIndex)
    {
        auto total = m_songsMatchedCount > 0 ? static_cast<uint32_t>(m_songsMatchedCount) : static_cast<uint32_t>(m_songsAllResults.size());
        if (m_songsTracks.Size() < total &&
            itemIndex + kPageAppendThreshold >= m_songsTracks.Size())
        {
            if (DatabaseService().IsInitialized())
            {
                RunDetached(AppendSongsPageAsync());
            }
            else
            {
                AppendSongsPage();
            }
        }
    }


    LastMusicPlayer::Backend::TrackQuery MainWindow::CurrentSongsQuery(uint32_t offset, uint32_t limit) const
    {
        LastMusicPlayer::Backend::TrackQuery query;
        query.Filter = m_songsFilter;
        query.Sort = m_songsSort;
        query.Offset = static_cast<int>(offset);
        query.Limit = static_cast<int>(limit);
        // Local files only. Anything streamed reaches the library through
        // History, the collection tabs and the playlists it was added to;
        // this tab is the one place that answers "what is on this disk",
        // including files that have never been played and so are in none
        // of those.
        query.IncludeRemote = false;
        query.ActiveOnly = true;
        return query;
    }


    void MainWindow::ApplySongsFilterSort()
    {
        m_songsAllResults.clear();
        m_songsTracks.Clear();
        m_songsMatchedCount = 0;
        m_songsMatchedSeconds = 0.0;
        m_songsPageLoading = false;
        ++m_songsPageLoadId;
        m_songsResultsValid = false;
        m_songsLoadState = LoadState::Loading;
        // The subtitle carries counts, not progress; the placeholder is the
        // loading signal now, so it keeps whatever UpdateSongsStats writes.
        m_songsSkeleton.BeginLoading(true);
        UpdateSongsStats();
        RunDetached(EnsureSongsHydratedAsync(true));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::EnsureSongsHydratedAsync(bool reset)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        auto epoch = reset ? ++m_songsHydrationEpoch : m_songsHydrationEpoch;

        if (reset)
        {
        }

        if (!DatabaseService().IsInitialized())
        {
            m_songsAllResults.clear();
            m_songsTracks.Clear();
            m_songsMatchedCount = static_cast<int>(m_queue.CurrentPlaylist.size());
            m_songsMatchedSeconds = 0.0;
            for (auto const& track : m_queue.CurrentPlaylist)
            {
                m_songsMatchedSeconds += track.DurationSeconds();
                auto copy = track;
                copy.Index(static_cast<int32_t>(m_songsAllResults.size() + 1));
                m_songsAllResults.push_back(copy);
            }
            AppendSongsPage();
            m_songsLoadState = LoadState::Loaded;
            m_songsResultsValid = true;
            UpdateSongsStats();
            m_songsSkeleton.EndLoading();
            co_return;
        }

        co_await AppendSongsPageAsync();
        if (epoch == m_songsHydrationEpoch)
        {
            m_songsLoadState = LoadState::Loaded;
            m_songsResultsValid = true;
        }
        m_songsSkeleton.EndLoading();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AppendSongsPageAsync()
    {
        auto lifetime = get_strong();
        if (m_songsPageLoading)
        {
            co_return;
        }
        if (m_songsMatchedCount > 0 && static_cast<int>(m_songsAllResults.size()) >= m_songsMatchedCount)
        {
            co_return;
        }

        auto dispatcher = this->DispatcherQueue();
        auto epoch = m_songsHydrationEpoch;
        auto offset = static_cast<uint32_t>(m_songsAllResults.size());
        auto limit = m_songsGridMode ? kSongsGridPageSize : kSongsListPageSize;
        auto query = CurrentSongsQuery(offset, limit);
        m_songsPageLoading = true;
        auto pageLoadId = ++m_songsPageLoadId;

        co_await winrt::resume_background();
        auto page = DatabaseService().LoadTrackPage(query);

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_songsHydrationEpoch || pageLoadId != m_songsPageLoadId)
        {
            if (pageLoadId == m_songsPageLoadId)
            {
                m_songsPageLoading = false;
            }
            co_return;
        }

        m_songsMatchedCount = page.TotalCount;
        m_songsMatchedSeconds = page.TotalSeconds;
        for (auto const& source : page.Tracks)
        {
            auto copy = source;
            copy.Index(static_cast<int32_t>(m_songsAllResults.size() + 1));
            ResolveArtworkPresentation(copy, L"track");
            m_songsAllResults.push_back(copy);
            m_songsTracks.Append(copy);
        }
        if (pageLoadId == m_songsPageLoadId)
        {
            m_songsPageLoading = false;
        }
        UpdateSongsStats();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadSongsQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();

        std::vector<winrt::Last_Music_Player::TrackInfo> queue;
        if (DatabaseService().IsInitialized())
        {
            auto query = CurrentSongsQuery(0, 0);
            co_await winrt::resume_background();
            queue = DatabaseService().LoadTracksForQuery(query);
            co_await wil::resume_foreground(dispatcher);
        }
        else
        {
            queue = m_songsAllResults;
        }

        if (queue.empty())
        {
            co_return;
        }
        if (!clickedTrack)
        {
            clickedTrack = queue.front();
        }
        QueueAndPlayVisible(queue, clickedTrack);
    }

    void MainWindow::SetSongsGridMode(bool gridMode)
    {
        m_songsGridMode = gridMode;
        EnsureAccentBrushes();
        using V = winrt::Microsoft::UI::Xaml::Visibility;
        SongsListSurface().Visibility(gridMode ? V::Collapsed : V::Visible);
        MusicListView().Visibility(gridMode ? V::Collapsed : V::Visible);
        SongsListHeader().Visibility(gridMode ? V::Collapsed : V::Visible);
        SongsGridView().Visibility(gridMode ? V::Visible : V::Collapsed);

        SongsListViewButton().Background(gridMode ? m_brushTransparent : m_brushAccentSoft);
        SongsGridViewButton().Background(gridMode ? m_brushAccentSoft : m_brushTransparent);

        auto total = m_songsMatchedCount > 0 ? static_cast<size_t>(m_songsMatchedCount) : m_songsAllResults.size();
        if (!gridMode && m_songsTracks.Size() < (std::min)(static_cast<size_t>(kSongsListPageSize), total))
        {
            if (DatabaseService().IsInitialized())
            {
                RunDetached(AppendSongsPageAsync());
            }
            else
            {
                AppendSongsPage();
            }
        }
    }

    void MainWindow::UpdateSongsStats()
    {
        size_t loadedCount = m_songsTracks.Size();
        // The query is the tab's whole scope now, so what it matched is also
        // its total. Reading m_libraryStats here would count the streamed
        // tracks this tab deliberately leaves out, and report a total the
        // listing could never reach.
        size_t matchedCount = m_songsMatchedCount > 0
            ? static_cast<size_t>(m_songsMatchedCount)
            : (DatabaseService().IsInitialized() ? m_songsAllResults.size() : m_queue.CurrentPlaylist.size());
        double totalSeconds = m_songsMatchedSeconds;
        if (totalSeconds <= 0.0 && !DatabaseService().IsInitialized())
        {
            for (auto const& t : m_queue.CurrentPlaylist)
            {
                totalSeconds += t.DurationSeconds();
            }
        }
        long long mins = static_cast<long long>(totalSeconds / 60.0);
        std::wstring s;
        if (matchedCount == 0)
        {
            s = L"No music on this PC yet. Add a folder to scan one.";
        }
        else
        {
            s = std::to_wstring(loadedCount) + L" loaded / "
                + std::to_wstring(matchedCount) + L" tracks \x00B7 "
                + std::to_wstring(mins) + L" min total";
        }
        SongsSubtitle().Text(winrt::hstring(s));
    }

    void MainWindow::SongsPlayAll_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_songsAllResults.empty() && m_songsMatchedCount == 0)
        {
            return;
        }

        RunDetached(LoadSongsQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo{ nullptr }));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SongsGridView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (clickedTrack)
        {
            RunDetached(LoadSongsQueueAndPlayAsync(clickedTrack));
        }
        co_return;
    }

    void MainWindow::SongsList_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        QueueContainerArtwork(args);
        if (!args.InRecycleQueue())
        {
            MaybeAppendSongsPage(args.ItemIndex());
        }
    }

    void MainWindow::SongsGrid_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        if (!args.InRecycleQueue())
        {
            MaybeAppendSongsPage(args.ItemIndex());
        }
    }


    void MainWindow::PlaySongsTrack(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return;
        }

        MusicListView().SelectedItem(track);
        RunDetached(LoadSongsQueueAndPlayAsync(track));
    }

    void MainWindow::PlayNextFromSongs(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track || !IsPlayableHomeTrack(track))
        {
            return;
        }

        auto key = CatalogSourceKey(track);
        if (key.empty())
        {
            return;
        }

        // Nothing playing yet → "Play next" is functionally "Play now".
        auto current = AudioPlayerService().GetCurrentTrack();
        if (!current)
        {
            PlaySongsTrack(track);
            return;
        }

        // Skip if it's already queued by the user (avoid stacking dupes from
        // accidental double right-clicks).
        for (auto const& queued : m_queue.UserQueue)
        {
            if (CatalogSourceKey(queued) == key)
            {
                RebuildUpNextQueue();
                return;
            }
        }

        // Explicit Up Next survives when another tile replaces the playback context.
        m_queue.UserQueue.insert(m_queue.UserQueue.begin(), track);
        RebuildUpNextQueue();
    }

    void MainWindow::AddSongsTrackToQueue(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track || !IsPlayableHomeTrack(track))
        {
            return;
        }

        auto key = CatalogSourceKey(track);
        if (key.empty())
        {
            return;
        }

        // Skip if it's already queued by the user.
        for (auto const& queued : m_queue.UserQueue)
        {
            if (CatalogSourceKey(queued) == key)
            {
                RebuildUpNextQueue();
                return;
            }
        }

        // Append to the end of UserQueue. Plays after any earlier explicit
        // queue items and after the current track / context items finish.
        m_queue.UserQueue.push_back(track);
        RebuildUpNextQueue();
    }

    void MainWindow::PlayNextFromSongsBulk(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks)
    {
        if (tracks.empty())
        {
            return;
        }

        std::unordered_set<std::wstring> existing;
        existing.reserve(m_queue.UserQueue.size() + tracks.size());
        for (auto const& queued : m_queue.UserQueue)
        {
            auto k = CatalogSourceKey(queued);
            if (!k.empty()) existing.insert(std::move(k));
        }

        // Build the prefix in input order, dedup'd against UserQueue and
        // itself, then insert as a single block so the original ordering
        // survives. Per-track PlayNextFromSongs would reverse the list.
        std::vector<winrt::Last_Music_Player::TrackInfo> toInsert;
        toInsert.reserve(tracks.size());
        for (auto const& track : tracks)
        {
            if (!track || !IsPlayableHomeTrack(track)) continue;
            auto key = CatalogSourceKey(track);
            if (key.empty()) continue;
            if (existing.find(key) != existing.end()) continue;
            existing.insert(key);
            toInsert.push_back(track);
        }

        if (toInsert.empty())
        {
            RebuildUpNextQueue();
            return;
        }

        m_queue.UserQueue.insert(m_queue.UserQueue.begin(), toInsert.begin(), toInsert.end());
        RebuildUpNextQueue();
    }

    void MainWindow::AddSongsTracksToQueueBulk(std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks)
    {
        if (tracks.empty())
        {
            return;
        }

        std::unordered_set<std::wstring> existing;
        existing.reserve(m_queue.UserQueue.size() + tracks.size());
        for (auto const& queued : m_queue.UserQueue)
        {
            auto k = CatalogSourceKey(queued);
            if (!k.empty()) existing.insert(std::move(k));
        }

        bool anyAppended = false;
        for (auto const& track : tracks)
        {
            if (!track || !IsPlayableHomeTrack(track)) continue;
            auto key = CatalogSourceKey(track);
            if (key.empty()) continue;
            if (existing.find(key) != existing.end()) continue;
            existing.insert(key);
            m_queue.UserQueue.push_back(track);
            anyAppended = true;
        }

        if (anyAppended)
        {
            RebuildUpNextQueue();
        }
    }


    void MainWindow::SongsRowPlay_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        PlaySongsTrack(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        PlaySongsTrack(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        PlayNextFromSongs(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        AddSongsTrackToQueue(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuLike_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        ToggleTrackLiked(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuArtist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        OpenSongsTrackArtist(TrackFromActionSender(sender));
    }

    void MainWindow::BrowseRowMenuAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        OpenSongsTrackAlbum(TrackFromActionSender(sender));
    }

    void MainWindow::SongsListView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSongsGridMode(false);
    }

    void MainWindow::SongsGridView_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSongsGridMode(true);
    }

    void MainWindow::SongsSort_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto tag = ReadTagString(item.Tag());
        if (!tag.empty())
        {
            m_songsSort = std::wstring(tag.c_str());
            SongsSortLabel().Text(item.Text());
            ApplySongsFilterSort();
        }
    }

    void MainWindow::Row_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        if (auto fe = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (auto n = fe.FindName(L"RowNum").try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                n.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto p = fe.FindName(L"RowPlay").try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
        }
    }

    void MainWindow::Row_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        if (auto fe = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (auto n = fe.FindName(L"RowNum").try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                n.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            if (auto p = fe.FindName(L"RowPlay").try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                p.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
        }
    }

}
