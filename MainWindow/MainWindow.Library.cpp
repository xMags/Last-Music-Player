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
            LibTabAlbums(), LibTabArtists(), LibTabSongs(), LibTabHistory(), LibTabGenres(), LibTabPlaylists()
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
            tag = L"Albums";
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
        setVisibility(LibSongsPanel(), (tag == L"Songs" || tag == L"History") ? V::Visible : V::Collapsed);
        setVisibility(LibGenresPanel(), tag == L"Genres" ? V::Visible : V::Collapsed);
        setVisibility(LibPlaylistsPanel(), tag == L"Playlists" ? V::Visible : V::Collapsed);
        auto scopedTab = tag == L"Songs" || tag == L"History" || tag == L"Playlists";
        auto accountScopeAvailable = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && RemoteMusicServiceService().IsModeAvailable(LastMusicPlayer::Backend::RemoteAccessMode::Account);
        if (LibraryScopeSelector())
        {
            LibraryScopeSelector().Visibility(scopedTab && accountScopeAvailable ? V::Visible : V::Collapsed);
        }
        if (tag == L"Songs" || tag == L"History")
        {
            auto nextFilter = tag == L"History" ? std::wstring{ L"History" } : std::wstring{ L"All" };
            if (m_librarySongsFilter != nextFilter)
            {
                m_librarySongsFilter = std::move(nextFilter);
                m_librarySongsState = LoadState::Dirty;
            }
        }
        UpdateLibraryActionButtons();
        if (LibraryViewContainer() && LibraryViewContainer().Visibility() == V::Visible)
        {
            RunDetached(HydrateLibraryTabAsync(winrt::hstring(tag), false));
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
        if (LibSongsPanel().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            RunDetached(HydrateLibraryTabAsync(winrt::hstring(m_librarySongsFilter == L"History" ? L"History" : L"Songs"), true));
        }
        else if (LibPlaylistsPanel().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            RunDetached(HydrateLibraryTabAsync(L"Playlists", true));
        }
    }


}
