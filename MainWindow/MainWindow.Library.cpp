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

    void MainWindow::LibraryButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ShowPrimaryView(L"Library");
        HideLibraryDetail();
        if (LibTabPlaylists())
        {
            LibTabPlaylists().IsChecked(true);
            LibraryTab_Checked(LibTabPlaylists(), nullptr);
        }
        RunDetached(HydrateLibraryTabAsync(L"Playlists", false));
    }

    void MainWindow::LibraryTab_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto clicked = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!clicked)
        {
            return;
        }
        if (!m_xamlReadyForEvents)
        {
            return;
        }
        // Single-select: uncheck every other tab.
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton tabs[] = {
            LibTabPlaylists(), LibTabHistory(), LibTabAlbums(), LibTabArtists(), LibTabGenres(), LibTabSongs()
        };
        for (auto const& t : tabs)
        {
            if (t && t != clicked)
            {
                t.IsChecked(false);
            }
        }

        auto tag = ReadTagString(clicked.Tag());
        if (tag.empty())
        {
            tag = L"Playlists";
        }
        HideLibraryDetail();
        using V = winrt::Microsoft::UI::Xaml::Visibility;
        auto setVisibility = [](winrt::Microsoft::UI::Xaml::UIElement const& element, V visibility)
        {
            if (element)
            {
                element.Visibility(visibility);
            }
        };
        setVisibility(LibAlbumsGrid(), tag == L"Albums" ? V::Visible : V::Collapsed);
        setVisibility(LibArtistsGrid(), tag == L"Artists" ? V::Visible : V::Collapsed);
        setVisibility(LibrarySongsBrowser(), tag == L"Songs" ? V::Visible : V::Collapsed);
        setVisibility(LibHistoryPanel(), tag == L"History" ? V::Visible : V::Collapsed);
        setVisibility(LibGenresPanel(), tag == L"Genres" ? V::Visible : V::Collapsed);
        setVisibility(LibPlaylistsPanel(), tag == L"Playlists" ? V::Visible : V::Collapsed);
        auto scopedTab = tag == L"History" || tag == L"Playlists";
        auto accountScopeAvailable = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && RemoteMusicServiceService().IsModeAvailable(LastMusicPlayer::Backend::RemoteAccessMode::Account);
        if (LibraryScopeSelector())
        {
            LibraryScopeSelector().Visibility(scopedTab && accountScopeAvailable ? V::Visible : V::Collapsed);
        }
        UpdateLibraryActionButtons();
        if (!LibraryViewContainer() || LibraryViewContainer().Visibility() != V::Visible)
        {
            return;
        }
        if (tag == L"Songs")
        {
            if (!m_songsResultsValid)
            {
                ApplySongsFilterSort();
            }
            return;
        }
        RunDetached(HydrateLibraryTabAsync(winrt::hstring(tag), false));
    }

    void MainWindow::HistoryListView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetHistoryGridMode(false);
    }

    void MainWindow::HistoryGridView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetHistoryGridMode(true);
    }

    void MainWindow::HistorySort_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto tag = ReadTagString(item.Tag());
        if (tag.empty() || m_libraryHistorySort == tag.c_str())
        {
            return;
        }

        m_libraryHistorySort = tag.c_str();
        HistorySortLabel().Text(item.Text());
        m_librarySongsState = LoadState::Dirty;
        RunDetached(HydrateLibraryTabAsync(L"History", true));
    }

    void MainWindow::SetHistoryGridMode(bool gridMode)
    {
        m_libraryHistoryGridMode = gridMode;
        EnsureAccentBrushes();

        using V = winrt::Microsoft::UI::Xaml::Visibility;
        LibrarySongsListView().Visibility(gridMode ? V::Collapsed : V::Visible);
        HistoryGridView().Visibility(gridMode ? V::Visible : V::Collapsed);
        HistoryListViewButton().Background(gridMode ? m_brushTransparent : m_brushAccentSoft);
        HistoryGridViewButton().Background(gridMode ? m_brushAccentSoft : m_brushTransparent);

        auto total = m_librarySongsMatchedCount > 0
            ? static_cast<size_t>(m_librarySongsMatchedCount)
            : m_librarySongAllResults.size();
        if (!gridMode
            && m_librarySongs.Size() < (std::min)(static_cast<size_t>(kLibrarySongPageSize), total))
        {
            if (DatabaseService().IsInitialized())
            {
                RunDetached(AppendLibrarySongsPageAsync());
            }
            else
            {
                AppendLibrarySongsPage();
            }
        }
    }

    void MainWindow::UpdateHistoryCount()
    {
        auto count = m_librarySongsMatchedCount > 0
            ? static_cast<size_t>(m_librarySongsMatchedCount)
            : m_librarySongAllResults.size();
        HistoryCountText().Text(
            winrt::to_hstring(count) + (count == 1 ? L" track" : L" tracks"));
    }

    void MainWindow::ResetLibraryScopeToAll()
    {
        if (m_libraryScope != L"All")
        {
            m_libraryScope = L"All";
            m_librarySongsState = LoadState::Dirty;
            m_libraryPlaylistsState = LoadState::Dirty;
        }
        if (LibraryScopeSelector() && LibraryScopeSelector().SelectedIndex() != 0)
        {
            LibraryScopeSelector().SelectedIndex(0);
        }
    }

    void MainWindow::OpenLibraryHistory()
    {
        ResetLibraryScopeToAll();
        ShowPrimaryView(L"Library");
        HideLibraryDetail();
        if (!LibTabHistory())
        {
            return;
        }

        auto checked = LibTabHistory().IsChecked();
        if (!checked || !checked.Value())
        {
            LibTabHistory().IsChecked(true);
        }
        else
        {
            LibraryTab_Checked(LibTabHistory(), nullptr);
        }
    }

    void MainWindow::OpenLibraryPlaylists(bool autoMixes)
    {
        ResetLibraryScopeToAll();
        ShowPrimaryView(L"Library");
        HideLibraryDetail();
        if (!LibTabPlaylists())
        {
            return;
        }

        auto tabChecked = LibTabPlaylists().IsChecked();
        if (!tabChecked || !tabChecked.Value())
        {
            LibTabPlaylists().IsChecked(true);
        }
        else
        {
            LibraryTab_Checked(LibTabPlaylists(), nullptr);
        }

        auto filter = autoMixes ? LibPlaylistAutoFilter() : LibPlaylistManualFilter();
        if (!filter)
        {
            return;
        }
        auto filterChecked = filter.IsChecked();
        if (!filterChecked || !filterChecked.Value())
        {
            filter.IsChecked(true);
        }
        else
        {
            LibraryPlaylistFilter_Checked(filter, nullptr);
        }
    }

    void MainWindow::OpenLibraryAutoMixes()
    {
        OpenLibraryPlaylists(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenLibrarySystemPlaylistAsync(
        winrt::hstring playlistKey)
    {
        auto lifetime = get_strong();
        OpenLibraryPlaylists(false);
        co_await HydrateLibraryTabAsync(L"Playlists", true);

        if (!LibraryViewContainer()
            || LibraryViewContainer().Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible
            || m_libraryPlaylistFilter != L"Manual")
        {
            co_return;
        }
        auto tabChecked = LibTabPlaylists().IsChecked();
        if (!tabChecked || !tabChecked.Value())
        {
            co_return;
        }

        for (uint32_t index = 0; index < m_yourPlaylists.Size(); ++index)
        {
            auto playlist = m_yourPlaylists.GetAt(index);
            if (playlist.SourceKind() != L"auto-playlist"
                || playlist.SourceUrl() != playlistKey)
            {
                continue;
            }

            ShowLibraryDetail(
                playlist.SourceKind(),
                playlist.SourceUrl(),
                playlist.Title(),
                playlist.Artist(),
                ApprovedDetailArtwork(playlist, playlist.SourceKind()),
                playlist);
            co_return;
        }
    }

    void MainWindow::LibraryPlaylistFilter_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto clicked = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!clicked)
        {
            return;
        }

        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton filters[] = {
            LibPlaylistManualFilter(), LibPlaylistAutoFilter()
        };
        for (auto const& filter : filters)
        {
            if (filter && filter != clicked)
            {
                filter.IsChecked(false);
            }
        }

        auto tag = ReadTagString(clicked.Tag());
        m_libraryPlaylistFilter = tag == L"Auto" ? L"Auto" : L"Manual";
        if (!m_xamlReadyForEvents || !LibManualPlaylistsGrid() || !LibAutoPlaylistsGrid())
        {
            return;
        }

        using V = winrt::Microsoft::UI::Xaml::Visibility;
        LibManualPlaylistsGrid().Visibility(m_libraryPlaylistFilter == L"Manual" ? V::Visible : V::Collapsed);
        LibAutoPlaylistsGrid().Visibility(m_libraryPlaylistFilter == L"Auto" ? V::Visible : V::Collapsed);
        UpdateLibraryActionButtons();
    }

    void MainWindow::LibraryScope_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_xamlReadyForEvents || !LibraryScopeSelector())
        {
            return;
        }

        auto selected = LibraryScopeSelector().SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem>();
        auto scope = selected ? ReadTagString(selected.Tag()) : winrt::hstring{ L"All" };
        auto nextScope = scope == L"Account" ? std::wstring{ L"Account" }
            : (scope == L"OnThisPc" ? std::wstring{ L"OnThisPc" } : std::wstring{ L"All" });
        if (m_libraryScope == nextScope)
        {
            return;
        }

        m_libraryScope = std::move(nextScope);
        m_librarySongsState = LoadState::Dirty;
        m_libraryPlaylistsState = LoadState::Dirty;
        UpdateLibraryActionButtons();
        if (!LibraryViewContainer() || LibraryViewContainer().Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            return;
        }
        if (LibHistoryPanel().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            RunDetached(HydrateLibraryTabAsync(L"History", true));
        }
        else if (LibPlaylistsPanel().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            RunDetached(HydrateLibraryTabAsync(L"Playlists", true));
        }
    }


}
