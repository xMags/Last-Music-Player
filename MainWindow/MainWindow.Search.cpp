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
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        // A pasted link (http/https) is resolved server-side to a single track.
        // Such lookups must never be served from — or written to — the in-memory
        // search cache: the resolver's metadata can change between attempts
        // (e.g. an extraction that previously failed and showed the raw URL now
        // succeeds), so a stale cache entry would keep showing the old result.
        // Plain-text queries are still cached normally.
        bool LooksLikeUrlQuery(winrt::hstring const& value)
        {
            std::wstring_view text{ value };
            auto const start = text.find_first_not_of(L" \t\r\n");
            if (start == std::wstring_view::npos)
            {
                return false;
            }
            text.remove_prefix(start);

            auto startsWithCi = [&](std::wstring_view prefix)
            {
                if (text.size() < prefix.size())
                {
                    return false;
                }
                for (size_t i = 0; i < prefix.size(); ++i)
                {
                    wchar_t c = text[i];
                    if (c >= L'A' && c <= L'Z')
                    {
                        c = static_cast<wchar_t>(c - L'A' + L'a');
                    }
                    if (c != prefix[i])
                    {
                        return false;
                    }
                }
                return true;
            };

            return startsWithCi(L"https://") || startsWithCi(L"http://");
        }
    }

    void MainWindow::GlobalSearchBox_GotFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        EnsureAccentBrushes();
        SearchBorder().BorderBrush(m_brushAccent);
        SearchGlow().BorderBrush(m_brushAccentSoft);
    }

    void MainWindow::GlobalSearchBox_LostFocus(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        EnsureAccentBrushes();
        SearchBorder().BorderBrush(m_brushStroke);
        SearchGlow().BorderBrush(m_brushTransparent);
    }

    void MainWindow::GlobalSearchBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        (void)sender;
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }

        args.Handled(true);
        RunHomeSearchAsync();
    }

    void MainWindow::HomeSearchButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        RunHomeSearchAsync();
    }


    void MainWindow::EnterSearchMode(winrt::hstring const& query)
    {
        (void)query;
        m_isSearchMode = true;
        HomeDefaultContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        HomeSearchContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        if (m_searchTracks.Size() == 0)
        {
            SearchStatusText().Text(L"Searching");
        }
    }

    void MainWindow::ExitSearchMode()
    {
        ++m_searchDebounceId;
        ++m_searchRequestId;
        m_isSearchMode = false;
        m_searchTracks.Clear();
        HomeSearchContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        HomeDefaultContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        SearchStatusText().Text(L"Type to search");
        if (!GlobalSearchBox().Text().empty())
        {
            GlobalSearchBox().Text(L"");
        }
        RunDetached(HydrateHomeAsync(false));
    }

    void MainWindow::GlobalSearchBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        auto query = TrimQuery(GlobalSearchBox().Text());
        if (query.empty())
        {
            ExitSearchMode();
            return;
        }

        EnterSearchMode(query);
        RunDebouncedHomeSearch();
    }

    void MainWindow::RunDebouncedHomeSearch()
    {
        auto const debounceId = ++m_searchDebounceId;
        auto const query = TrimQuery(GlobalSearchBox().Text());
        [](winrt::weak_ref<MainWindow> weakThis,
           winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
           uint64_t debounceId,
           winrt::hstring query) -> winrt::fire_and_forget
        {
            co_await winrt::resume_after(std::chrono::milliseconds(350));
            dispatcher.TryEnqueue([weakThis, debounceId, query]()
            {
                auto self = weakThis.get();
                if (!self || !self->m_isSearchMode || debounceId != self->m_searchDebounceId)
                {
                    return;
                }

                auto const liveQuery = TrimQuery(self->GlobalSearchBox().Text());
                if (liveQuery.empty() || liveQuery != query)
                {
                    return;
                }

                auto const requestId = ++self->m_searchRequestId;
                RunDetached(self->RunHomeSearchNowAsync(query, requestId));
            });
        }(get_weak(), DispatcherQueue(), debounceId, query);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RunHomeSearchAsync()
    {
        auto query = TrimQuery(GlobalSearchBox().Text());
        if (query.empty())
        {
            ExitSearchMode();
            co_return;
        }

        ++m_searchDebounceId;
        EnterSearchMode(query);
        auto const requestId = ++m_searchRequestId;
        co_await RunHomeSearchNowAsync(query, requestId);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RunHomeSearchNowAsync(winrt::hstring query, uint64_t requestId)
    {
        if (requestId != m_searchRequestId || !m_isSearchMode)
        {
            co_return;
        }

        m_searchTracks.Clear();
        SearchStatusText().Text(L"Searching");

        std::unordered_map<std::wstring, int> visibleKeys;
        auto appendVisibleTrack = [&](winrt::Last_Music_Player::TrackInfo const& track) -> bool
        {
            auto key = HomeQueueDedupeKey(track);
            if (visibleKeys.find(key) != visibleKeys.end())
            {
                return false;
            }

            visibleKeys.emplace(std::move(key), static_cast<int>(m_searchTracks.Size()));
            m_searchTracks.Append(track);
            return true;
        };

        size_t localMatches = 0;
        for (auto const& track : m_queue.CurrentPlaylist)
        {
            if (ContainsFolded(track.Title(), query) ||
                ContainsFolded(track.Artist(), query) ||
                ContainsFolded(track.Album(), query))
            {
                if (appendVisibleTrack(track))
                {
                    ++localMatches;
                }
                // Cap at 60 (instead of 15) so the local-library half of the
                // results doesn't silently swallow matches when the user's
                // library is large. The provider service typically returns
                // ~30 unique results across all sources, so 60 total gives
                // both halves room to fill without artificial truncation.
                if (localMatches >= 60)
                {
                    break;
                }
            }
        }

        auto updateFinalStatus = [&]()
        {
            auto const total = static_cast<uint32_t>(m_searchTracks.Size());
            if (total == 0)
            {
                SearchStatusText().Text(L"No matches");
            }
            else
            {
                SearchStatusText().Text(winrt::to_hstring(total) + L" Results");
            }
        };

        if (localMatches > 0)
        {
            SearchStatusText().Text(L"Searching remote");
        }

        size_t remoteMatches = 0;
        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        if (remoteScope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
            || !remoteMusic.IsModeAvailable(remoteScope.Mode)
            || !remoteMusic.IsCurrent(remoteScope))
        {
            SearchStatusText().Text(m_searchTracks.Size() > 0 ? L"Local only" : L"Remote unavailable");
            co_return;
        }
        auto cacheScope = LastMusicPlayer::Backend::RemoteScopeCacheKey(remoteScope);

        try
        {
            bool const isUrlQuery = LooksLikeUrlQuery(query);
            auto cacheKey = cacheScope + L"\n" + std::wstring(query.c_str());
            std::vector<winrt::Last_Music_Player::TrackInfo> remoteTracks;
            bool loadedFromCache{};

            // Pasted links bypass the cache entirely (see LooksLikeUrlQuery).
            auto cached = isUrlQuery ? m_remoteSearchCache.end() : m_remoteSearchCache.find(cacheKey);
            if (cached != m_remoteSearchCache.end())
            {
                remoteTracks = cached->second;
                loadedFromCache = true;
            }
            else
            {
                auto payload = co_await remoteMusic.SearchAsync(remoteScope, query);
                if (requestId != m_searchRequestId
                    || !m_isSearchMode
                    || !remoteMusic.IsCurrent(remoteScope))
                {
                    co_return;
                }

                // Accept the provider's bounded result set. A smaller client-side
                // cap previously truncated useful matches for longer queries.
                remoteTracks = ParseProviderTracks(payload, 60);
            }

            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }
            for (auto const& track : remoteTracks)
            {
                if (appendVisibleTrack(track))
                {
                    ++remoteMatches;
                }
                if (remoteMatches >= 60)
                {
                    break;
                }
            }
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }

            // URL lookups are intentionally not cached, so re-pasting the same
            // link always reflects the latest server-side resolution.
            if (!loadedFromCache && !isUrlQuery)
            {
                if (remoteTracks.empty())
                {
                    m_remoteSearchCache.erase(cacheKey);
                }
                else
                {
                    if (m_remoteSearchCache.size() >= kRemoteSearchCacheLimit)
                    {
                        m_remoteSearchCache.clear();
                    }
                    m_remoteSearchCache[cacheKey] = remoteTracks;
                }
            }
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (winrt::hresult_error const&)
        {
            SearchStatusText().Text(m_searchTracks.Size() > 0 ? L"Local only" : L"Remote failed");
            co_return;
        }
        catch (...)
        {
            SearchStatusText().Text(m_searchTracks.Size() > 0 ? L"Local only" : L"Remote failed");
            co_return;
        }

        if (requestId != m_searchRequestId || !m_isSearchMode)
        {
            co_return;
        }

        updateFinalStatus();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SearchResult_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }

        MusicListView().SelectedItem(clickedTrack);

        // Search is not a durable collection. Clicking a result plays only that
        // track; multi-track queueing stays an explicit action.
        std::vector<winrt::Last_Music_Player::TrackInfo> searchQueue{ clickedTrack };
        SetPlaybackQueue(searchQueue, 0);
        PlayTrack(clickedTrack);
    }

    void MainWindow::AccelSearch_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        (void)sender;
        // The search box only exists on Home — switch there, then focus it.
        HomeButton_Click(nullptr, nullptr);
        GlobalSearchBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        args.Handled(true);
    }

}
