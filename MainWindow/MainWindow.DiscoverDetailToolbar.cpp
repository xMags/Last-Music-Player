#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/DetailSortPolicy.h"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;

        using MUX::Visibility;

        constexpr std::chrono::milliseconds kDiscoverDetailFindDebounce{ 180 };
    }

    void MainWindow::ResetDiscoverDetailToolbar()
    {
        m_discoverDetailFindText.clear();
        m_discoverDetailSort = LastMusicPlayer::Backend::DetailSort::Curated;
        ++m_discoverDetailFindDebounceId;

        if (auto findBox = DiscoverDetailFindBox())
        {
            m_discoverDetailFindSuppressed = true;
            findBox.Text(L"");
            m_discoverDetailFindSuppressed = false;
        }
        if (auto label = DiscoverDetailSortLabel())
        {
            label.Text(L"Custom order");
        }
    }

    void MainWindow::RebuildDiscoverDetailProjection()
    {
        std::vector<LastMusicPlayer::Backend::DetailTrackSortKey> keys;
        keys.reserve(m_discoverDetailAllResults.size());
        for (auto const& track : m_discoverDetailAllResults)
        {
            LastMusicPlayer::Backend::DetailTrackSortKey key;
            if (track)
            {
                key.Title = track.Title().c_str();
                key.Artist = track.Artist().c_str();
                key.Album = track.Album().c_str();
                key.DurationSeconds = track.DurationSeconds();
            }
            keys.push_back(std::move(key));
        }

        auto const order = LastMusicPlayer::Backend::DetailTrackOrder(
            keys,
            m_discoverDetailFindText,
            m_discoverDetailSort);

        m_discoverDetailTracks.Clear();
        int32_t displayIndex = 1;
        for (auto const sourceIndex : order)
        {
            auto const track = m_discoverDetailAllResults[sourceIndex];
            if (!track)
            {
                continue;
            }
            track.Index(displayIndex++);
            m_discoverDetailTracks.Append(track);
        }

        UpdateDiscoverDetailEmptyState();
        RefreshDiscoverDetailRowStates();
    }

    void MainWindow::UpdateDiscoverDetailEmptyState()
    {
        auto const empty = DiscoverDetailEmptyFilterText();
        if (!empty)
        {
            return;
        }
        if (m_discoverDetailLoading || m_discoverDetailTracks.Size() > 0)
        {
            empty.Visibility(Visibility::Collapsed);
            return;
        }

        empty.Text(m_discoverDetailLoadFailed
            ? winrt::hstring{ L"Could not load these tracks." }
            : (m_discoverDetailAllResults.empty()
                ? winrt::hstring{ L"Nothing here yet." }
                : winrt::hstring(L"No tracks match “" + m_discoverDetailFindText + L"”")));
        empty.Visibility(Visibility::Visible);
    }

    void MainWindow::SetDiscoverDetailGridMode(bool gridMode)
    {
        m_discoverDetailGridMode = gridMode;
        EnsureAccentBrushes();

        if (auto listButton = DiscoverDetailListViewButton())
        {
            listButton.Background(gridMode ? m_brushTransparent : m_brushNeutralFill);
        }
        if (auto gridButton = DiscoverDetailGridViewButton())
        {
            gridButton.Background(gridMode ? m_brushNeutralFill : m_brushTransparent);
        }
        if (auto glyph = DiscoverDetailListViewGlyph())
        {
            glyph.Foreground(gridMode ? m_brushGlyphIdle : m_brushLabelIdle);
        }
        if (auto glyph = DiscoverDetailGridViewGlyph())
        {
            glyph.Foreground(gridMode ? m_brushLabelIdle : m_brushGlyphIdle);
        }
        if (auto surface = DiscoverDetailListSurface())
        {
            surface.Visibility(gridMode ? Visibility::Collapsed : Visibility::Visible);
        }
        if (auto grid = DiscoverDetailGridView())
        {
            grid.Visibility(gridMode ? Visibility::Visible : Visibility::Collapsed);
            if (!grid.ItemsSource())
            {
                grid.ItemsSource(m_discoverDetailTracks);
            }
        }
        RefreshDiscoverDetailRowStates();
    }

    void MainWindow::DiscoverDetailListView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetDiscoverDetailGridMode(false);
    }

    void MainWindow::DiscoverDetailGridView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetDiscoverDetailGridMode(true);
    }

    void MainWindow::DiscoverDetailFind_TextChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::TextChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_discoverDetailFindSuppressed)
        {
            return;
        }

        auto const debounceId = ++m_discoverDetailFindDebounceId;
        auto const typed = DiscoverDetailFindBox()
            ? std::wstring(detail::TrimQuery(DiscoverDetailFindBox().Text()).c_str())
            : std::wstring{};

        [](winrt::weak_ref<MainWindow> weakThis,
           winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
           uint64_t debounceId,
           std::wstring typed) -> winrt::fire_and_forget
        {
            co_await winrt::resume_after(kDiscoverDetailFindDebounce);
            dispatcher.TryEnqueue([weakThis, debounceId, typed = std::move(typed)]()
            {
                auto const self = weakThis.get();
                if (!self || debounceId != self->m_discoverDetailFindDebounceId)
                {
                    return;
                }
                if (self->m_discoverDetailFindText == typed)
                {
                    return;
                }
                self->m_discoverDetailFindText = typed;
                self->RebuildDiscoverDetailProjection();
            });
        }(get_weak(), DispatcherQueue(), debounceId, typed);
    }

    void MainWindow::DiscoverDetailSort_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto const item = sender.try_as<MUXC::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto const requested = LastMusicPlayer::Backend::ParseDetailSort(
            std::wstring(detail::ReadTagString(item.Tag()).c_str()),
            true);
        if (requested == m_discoverDetailSort)
        {
            return;
        }

        m_discoverDetailSort = requested;
        if (auto label = DiscoverDetailSortLabel())
        {
            label.Text(winrt::hstring(
                LastMusicPlayer::Backend::DetailSortLabel(m_discoverDetailSort)));
        }
        RebuildDiscoverDetailProjection();
    }

    void MainWindow::DiscoverDetailMenuPlayNext_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        tracks.reserve(m_discoverDetailTracks.Size());
        for (auto const& track : m_discoverDetailTracks)
        {
            tracks.push_back(track);
        }
        PlayNextFromSongsBulk(tracks);
    }

    void MainWindow::DiscoverDetailGrid_ContainerContentChanging(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        QueueContainerArtwork(args);
        if (args.InRecycleQueue() || args.ItemIndex() < 0)
        {
            return;
        }
        if (auto const container = args.ItemContainer().try_as<MUXC::GridViewItem>())
        {
            ApplyDiscoverDetailCardState(container, static_cast<uint32_t>(args.ItemIndex()));
        }
    }

    void MainWindow::ApplyDiscoverDetailCardState(
        MUXC::GridViewItem const& container,
        uint32_t index)
    {
        if (!container || index >= m_discoverDetailTracks.Size())
        {
            return;
        }
        auto const root = container.ContentTemplateRoot().try_as<MUXC::StackPanel>();
        if (!root || root.Children().Size() < 2)
        {
            return;
        }

        EnsureAccentBrushes();
        auto const track = m_discoverDetailTracks.GetAt(index);
        auto const current = detail::AudioPlayerService().GetCurrentTrack();
        auto const nowPlaying = track && current
            && detail::CatalogSourceKey(track) == detail::CatalogSourceKey(current);

        if (auto const art = root.Children().GetAt(0).try_as<MUXC::Border>())
        {
            auto const artGrid = art.Child().try_as<MUXC::Grid>();
            if (artGrid && artGrid.Children().Size() > 2)
            {
                if (auto const badge = artGrid.Children().GetAt(2).try_as<MUXC::Border>())
                {
                    badge.Visibility(nowPlaying ? Visibility::Visible : Visibility::Collapsed);
                }
            }
        }
        if (auto const title = root.Children().GetAt(1).try_as<MUXC::TextBlock>())
        {
            title.Foreground(nowPlaying ? m_brushAccent : m_brushLabelIdle);
        }
    }
}
