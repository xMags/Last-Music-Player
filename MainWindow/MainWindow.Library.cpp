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
#include <winrt/Microsoft.UI.Xaml.Media.Animation.h>
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
        UpdateLibraryHeaderMetrics();
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
            LibTabPlaylists(), LibTabHistory(), LibTabMostPlayed(), LibTabAlbums(),
            LibTabArtists(), LibTabGenres(), LibTabSongs()
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
        // History and Most Played are the same panel over a different query, so
        // switching between them only changes the filter the page loads with.
        auto tracksTab = tag == L"History" || tag == L"MostPlayed";
        if (tracksTab)
        {
            std::wstring wanted = tag == L"MostPlayed" ? L"Most" : L"History";
            if (m_libraryTracksFilter != wanted)
            {
                m_libraryTracksFilter = wanted;
                // Each tab's default order is its own "Relevance": most recent
                // for History, most played for Most Played.
                m_libraryHistorySort = L"Relevance";
                if (HistorySortLabel())
                {
                    HistorySortLabel().Text(L"Relevance");
                }
                m_librarySongsState = LoadState::Dirty;
            }
        }
        setVisibility(LibHistoryPanel(), tracksTab ? V::Visible : V::Collapsed);
        setVisibility(LibGenresPanel(), tag == L"Genres" ? V::Visible : V::Collapsed);
        setVisibility(LibPlaylistsPanel(), tag == L"Playlists" ? V::Visible : V::Collapsed);
        UpdateLibraryHeaderMetrics();
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
        UpdateLibraryHeaderMetrics();
    }

    void MainWindow::UpdateLibraryHeaderMetrics()
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto groupedNumber = [](std::uint64_t value)
        {
            auto text = std::to_wstring(value);
            for (std::ptrdiff_t index = static_cast<std::ptrdiff_t>(text.size()) - 3;
                 index > 0;
                 index -= 3)
            {
                text.insert(static_cast<std::size_t>(index), 1, L',');
            }
            return winrt::hstring{ text };
        };
        auto isChecked = [](winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button)
        {
            if (!button)
            {
                return false;
            }
            auto value = button.IsChecked();
            return value && value.Value();
        };

        auto hours = static_cast<std::uint64_t>(std::round(m_libraryStats.TotalSeconds / 3600.0));
        LibrarySubtitle().Text(
            groupedNumber(static_cast<std::uint64_t>((std::max)(0, m_libraryStats.SongCount))) + L" tracks  ·  "
            + groupedNumber(static_cast<std::uint64_t>((std::max)(0, m_libraryStats.AlbumCount))) + L" albums  ·  "
            + groupedNumber(static_cast<std::uint64_t>((std::max)(0, m_libraryStats.ArtistCount))) + L" artists  ·  "
            + groupedNumber(hours) + L" h");

        LibTabPlaylistsCount().Text(groupedNumber(m_yourPlaylists.Size() + m_autoPlaylists.Size()));
        auto trackCount = m_librarySongsMatchedCount > 0
            ? static_cast<std::uint64_t>(m_librarySongsMatchedCount)
            : static_cast<std::uint64_t>(m_librarySongAllResults.size());
        if (m_libraryTracksFilter == L"Most")
        {
            LibTabMostPlayedCount().Text(groupedNumber(trackCount));
        }
        else
        {
            LibTabHistoryCount().Text(groupedNumber(trackCount));
        }
        LibTabAlbumsCount().Text(groupedNumber(m_albums.Size()));
        LibTabArtistsCount().Text(groupedNumber(m_artists.Size()));
        LibTabGenresCount().Text(groupedNumber(m_libraryGenres.Size()));
        LibTabSongsCount().Text(groupedNumber(static_cast<std::uint64_t>((std::max)(0, m_libraryStats.SongCount))));

        LibTabPlaylistsBadge().Visibility(isChecked(LibTabPlaylists()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabHistoryBadge().Visibility(isChecked(LibTabHistory()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabMostPlayedBadge().Visibility(isChecked(LibTabMostPlayed()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabAlbumsBadge().Visibility(isChecked(LibTabAlbums()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabArtistsBadge().Visibility(isChecked(LibTabArtists()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabGenresBadge().Visibility(isChecked(LibTabGenres()) ? Visibility::Visible : Visibility::Collapsed);
        LibTabSongsBadge().Visibility(isChecked(LibTabSongs()) ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::LibraryArtworkBorder_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto border = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Border>();
        if (!border)
        {
            return;
        }
        auto track = border.Tag().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }

        std::wstring identity{ track.SourceKind().c_str() };
        identity += L":";
        identity += track.Title().c_str();
        std::uint32_t hash = 2166136261u;
        for (auto character : identity)
        {
            hash ^= static_cast<std::uint32_t>(character);
            hash *= 16777619u;
        }

        using ColorPair = std::pair<winrt::Windows::UI::Color, winrt::Windows::UI::Color>;
        constexpr std::array<ColorPair, 8> palettes{
            ColorPair{ { 255, 16, 171, 188 }, { 255, 55, 191, 176 } },
            ColorPair{ { 255, 107, 142, 254 }, { 255, 159, 145, 239 } },
            ColorPair{ { 255, 218, 116, 102 }, { 255, 233, 156, 118 } },
            ColorPair{ { 255, 58, 165, 108 }, { 255, 89, 194, 148 } },
            ColorPair{ { 255, 184, 92, 199 }, { 255, 220, 131, 192 } },
            ColorPair{ { 255, 16, 150, 202 }, { 255, 106, 164, 235 } },
            ColorPair{ { 255, 225, 138, 70 }, { 255, 236, 184, 93 } },
            ColorPair{ { 255, 47, 164, 143 }, { 255, 86, 197, 176 } },
        };
        auto const& palette = palettes[hash % palettes.size()];

        winrt::Microsoft::UI::Xaml::Media::LinearGradientBrush gradient;
        gradient.StartPoint({ 0, 0 });
        gradient.EndPoint({ 1, 1 });
        winrt::Microsoft::UI::Xaml::Media::GradientStop first;
        first.Color(palette.first);
        first.Offset(0.0);
        gradient.GradientStops().Append(first);
        winrt::Microsoft::UI::Xaml::Media::GradientStop second;
        second.Color(palette.second);
        second.Offset(1.0);
        gradient.GradientStops().Append(second);
        border.Background(gradient);
    }

    void MainWindow::OpenLibraryHistory()
    {
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

    namespace
    {
        template <typename T>
        T FindScrollHost(winrt::Microsoft::UI::Xaml::DependencyObject const& root)
        {
            if (!root)
            {
                return nullptr;
            }
            if (auto match = root.try_as<T>())
            {
                return match;
            }
            auto count = winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChildrenCount(root);
            for (int32_t index = 0; index < count; ++index)
            {
                if (auto found = FindScrollHost<T>(
                    winrt::Microsoft::UI::Xaml::Media::VisualTreeHelper::GetChild(root, index)))
                {
                    return found;
                }
            }
            return nullptr;
        }
    }

    void MainWindow::ObserveLibraryScroll(winrt::Microsoft::UI::Xaml::FrameworkElement const& surface)
    {
        if (!surface)
        {
            return;
        }

        // The ScrollViewer belongs to the list's template, and a tab that
        // starts collapsed is never measured, so at Loaded most of these
        // controls still have no template to search. The same attempt is
        // therefore made again on the first size change, which is what
        // showing the tab produces. The flag keeps it to one subscription.
        auto attached = std::make_shared<bool>(false);
        auto attach = [weak = get_weak(), attached](
            winrt::Windows::Foundation::IInspectable const& sender,
            auto const&)
        {
            if (*attached)
            {
                return;
            }
            auto self = weak.get();
            auto element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
            if (!self || !element)
            {
                return;
            }
            auto scroller = FindScrollHost<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>(element);
            if (!scroller)
            {
                return;
            }
            *attached = true;
            scroller.ViewChanged([weak](
                winrt::Windows::Foundation::IInspectable const& viewSender,
                winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&)
            {
                if (auto strong = weak.get())
                {
                    strong->UpdateLibraryHeaderForScroll(
                        viewSender.try_as<winrt::Microsoft::UI::Xaml::Controls::ScrollViewer>());
                }
            });
        };

        surface.Loaded(attach);
        surface.SizeChanged(attach);
    }

    void MainWindow::UpdateLibraryHeaderForScroll(
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer const& scroller)
    {
        auto header = LibraryHeaderBar();
        if (!scroller || !header)
        {
            return;
        }

        // Collapsing the header hands its height back to the list, so the two
        // are in a feedback loop: hide it and there may no longer be anything
        // to scroll, which would show it again. Two things break the loop.
        // Collapsing is only allowed when the list has more to scroll than the
        // header is tall, so it still has somewhere to sit afterwards; and the
        // thresholds are apart, so the offset the collapse produces cannot
        // immediately satisfy the expand test.
        constexpr double collapseAt = 56.0;
        constexpr double expandAt = 8.0;
        auto offset = scroller.VerticalOffset();
        if (m_libraryHeaderCollapsed)
        {
            if (offset <= expandAt)
            {
                SetLibraryHeaderCollapsed(false);
            }
            return;
        }
        if (offset > collapseAt && scroller.ScrollableHeight() > header.ActualHeight() + 24.0)
        {
            SetLibraryHeaderCollapsed(true);
        }
    }

    void MainWindow::SetLibraryHeaderCollapsed(bool collapsed)
    {
        if (m_libraryHeaderCollapsed == collapsed)
        {
            return;
        }
        auto header = LibraryHeaderBar();
        if (!header)
        {
            return;
        }
        m_libraryHeaderCollapsed = collapsed;

        namespace animation = winrt::Microsoft::UI::Xaml::Media::Animation;
        using winrt::Microsoft::UI::Xaml::Visibility;

        // Measured once while the header is up. Height has to animate to a
        // number, and reading it during the collapse would read whatever the
        // animation had reached.
        if (!collapsed || m_libraryHeaderHeight <= 0.0)
        {
            auto natural = header.ActualHeight();
            if (natural > 0.0)
            {
                m_libraryHeaderHeight = natural;
            }
        }
        if (m_libraryHeaderHeight <= 0.0)
        {
            header.Visibility(collapsed ? Visibility::Collapsed : Visibility::Visible);
            return;
        }

        if (!collapsed)
        {
            header.Visibility(Visibility::Visible);
        }

        animation::Storyboard storyboard;

        // Height is a layout property, so this is a dependent animation and has
        // to say so. It is one small element rather than a list, which is what
        // that flag exists to keep people away from.
        animation::DoubleAnimation heightStep;
        heightStep.EnableDependentAnimation(true);
        heightStep.From(collapsed ? m_libraryHeaderHeight : 0.0);
        heightStep.To(collapsed ? 0.0 : m_libraryHeaderHeight);
        heightStep.Duration(winrt::Microsoft::UI::Xaml::DurationHelper::FromTimeSpan(
            std::chrono::milliseconds{ 180 }));
        animation::CubicEase heightEase;
        heightEase.EasingMode(animation::EasingMode::EaseOut);
        heightStep.EasingFunction(heightEase);
        animation::Storyboard::SetTarget(heightStep, header);
        animation::Storyboard::SetTargetProperty(heightStep, L"Height");
        storyboard.Children().Append(heightStep);

        // Fades a little ahead of the collapse so the text is gone before the
        // row is, rather than being squashed on the way out.
        animation::DoubleAnimation fadeStep;
        fadeStep.From(collapsed ? 1.0 : 0.0);
        fadeStep.To(collapsed ? 0.0 : 1.0);
        fadeStep.Duration(winrt::Microsoft::UI::Xaml::DurationHelper::FromTimeSpan(
            std::chrono::milliseconds{ collapsed ? 110 : 180 }));
        animation::Storyboard::SetTarget(fadeStep, header);
        animation::Storyboard::SetTargetProperty(fadeStep, L"Opacity");
        storyboard.Children().Append(fadeStep);

        storyboard.Completed([weak = get_weak(), collapsed](
            winrt::Windows::Foundation::IInspectable const&,
            winrt::Windows::Foundation::IInspectable const&)
        {
            auto self = weak.get();
            if (!self)
            {
                return;
            }
            auto bar = self->LibraryHeaderBar();
            if (!bar)
            {
                return;
            }
            // A scroll back to the top can land mid-collapse; only the state
            // that is still current gets to finish the job.
            if (self->m_libraryHeaderCollapsed != collapsed)
            {
                return;
            }
            if (collapsed)
            {
                bar.Visibility(Visibility::Collapsed);
            }
            else
            {
                // Released back to Auto so the header keeps following its own
                // content, which the responsive handler still reflows.
                bar.Height(std::numeric_limits<double>::quiet_NaN());
            }
        });

        storyboard.Begin();
    }

}
