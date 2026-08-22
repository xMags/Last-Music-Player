#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/DetailSortPolicy.h"
#include "Backend/PlaylistSuggestionPolicy.h"

#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Windows.UI.Core.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;

        using MUX::Visibility;

        // How long the find field waits after the last keystroke before it
        // re-queries. Shorter than the catalog search's 350 ms because this
        // one only hits the local database.
        constexpr std::chrono::milliseconds kFindDebounce{ 180 };

        // Suggestions are a shelf, not a list: enough to offer a real choice
        // without pushing the track surface off the page.
        constexpr std::size_t kSuggestionLimit = 5;

        // Positions inside the detail row template. The row is a Grid this
        // file's own DataTemplate defines, so these are stable, but every
        // lookup is still guarded: a template edit that reorders the cells
        // should degrade to "no now-playing highlight", never crash.
        constexpr uint32_t kRowIndexTextChild = 0;
        constexpr uint32_t kRowPlayGlyphChild = 1;
        constexpr uint32_t kRowTitleBlockChild = 2;

        MUXC::TextBlock RowTitleTextBlock(MUXC::Grid const& row)
        {
            if (!row || row.Children().Size() <= kRowTitleBlockChild)
            {
                return nullptr;
            }
            auto titleCell = row.Children().GetAt(kRowTitleBlockChild).try_as<MUXC::Grid>();
            if (!titleCell || titleCell.Children().Size() < 2)
            {
                return nullptr;
            }
            auto stack = titleCell.Children().GetAt(1).try_as<MUXC::StackPanel>();
            if (!stack || stack.Children().Size() == 0)
            {
                return nullptr;
            }
            return stack.Children().GetAt(0).try_as<MUXC::TextBlock>();
        }

        std::wstring FoldedText(winrt::hstring const& value)
        {
            return detail::ToLowerCopy(value);
        }
    }

    // ---------------------------------------------------------------- state

    bool MainWindow::LibraryDetailSupportsCuratedOrder() const
    {
        return LastMusicPlayer::Backend::KindSupportsCuratedOrder(m_libraryDetailKind);
    }

    bool MainWindow::IsLibraryDetailPlaylist() const
    {
        return m_libraryDetailKind == L"playlist";
    }

    void MainWindow::ResetLibraryDetailToolbar()
    {
        m_libraryDetailFindText.clear();
        m_libraryDetailSort = LastMusicPlayer::Backend::DefaultDetailSort(
            LibraryDetailSupportsCuratedOrder());
        ++m_libraryDetailFindDebounceId;
        ClearLibraryDetailSelection();

        if (auto findBox = LibraryDetailFindBox())
        {
            // Assigning Text raises TextChanged, which would queue a redundant
            // reload of the detail we are in the middle of opening. The guard
            // is cleared before the caller hydrates.
            m_libraryDetailFindSuppressed = true;
            findBox.Text(L"");
            m_libraryDetailFindSuppressed = false;
        }
        if (auto label = LibraryDetailSortLabel())
        {
            label.Text(winrt::hstring(
                LastMusicPlayer::Backend::DetailSortLabel(m_libraryDetailSort)));
        }
    }

    void MainWindow::ApplyLibraryDetailKindAffordances()
    {
        auto const playlist = IsLibraryDetailPlaylist();
        auto const curated = LibraryDetailSupportsCuratedOrder();

        auto show = [](MUX::UIElement const& element, bool visible)
        {
            if (element)
            {
                element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
            }
        };

        show(LibraryDetailEditButton(), playlist);
        show(LibraryDetailMenuRename(), playlist);
        show(LibraryDetailMenuDelete(), playlist);
        show(LibraryDetailSortCustom(), curated);
        show(LibraryDetailSelectionMoveButton(), playlist);
        show(LibraryDetailSelectionRemoveButton(), playlist);

        if (auto findBox = LibraryDetailFindBox())
        {
            findBox.PlaceholderText(playlist ? L"Find in playlist" : L"Find in this list");
        }
        if (auto move = LibraryDetailSelectionMoveButton())
        {
            move.Content(winrt::box_value(winrt::hstring(L"Move to playlist")));
        }

        auto const breadcrumbSection = [&]() -> winrt::hstring
        {
            if (m_libraryDetailKind == L"artist") { return L"Artists"; }
            if (m_libraryDetailKind == L"genre") { return L"Genres"; }
            if (m_libraryDetailKind == L"album" || m_libraryDetailKind == L"album-collection")
            {
                return L"Albums";
            }
            return L"Playlists";
        }();
        if (auto section = LibraryDetailBreadcrumbSection())
        {
            section.Content(winrt::box_value(breadcrumbSection));
        }
    }

    // ------------------------------------------------------------ hero meta

    void MainWindow::UpdateLibraryDetailHeroMeta()
    {
        auto setText = [](MUXC::TextBlock const& block, std::wstring const& text)
        {
            if (!block)
            {
                return;
            }
            block.Text(winrt::hstring(text));
            block.Visibility(text.empty() ? Visibility::Collapsed : Visibility::Visible);
        };

        // Duration, rounded the way a listener reads it: whole minutes under an
        // hour, hours and minutes above.
        std::wstring duration;
        if (m_libraryDetailMatchedSeconds >= 60.0)
        {
            auto const totalMinutes = static_cast<int>(m_libraryDetailMatchedSeconds / 60.0);
            auto const hours = totalMinutes / 60;
            auto const minutes = totalMinutes % 60;
            if (hours > 0)
            {
                duration = std::to_wstring(hours) + L" h " + std::to_wstring(minutes) + L" min";
            }
            else
            {
                duration = std::to_wstring(minutes) + L" min";
            }
        }
        setText(LibraryDetailDurationText(), duration);

        // Offline count reports what has actually been loaded, so it must not
        // claim a total the paged list has not seen yet.
        std::wstring offline;
        auto const offlineCount = static_cast<int>(std::count_if(
            m_libraryDetailAllResults.begin(),
            m_libraryDetailAllResults.end(),
            [](winrt::Last_Music_Player::TrackInfo const& track)
            {
                return track && track.IsOffline();
            }));
        if (offlineCount > 0
            && static_cast<int>(m_libraryDetailAllResults.size()) >= m_libraryDetailMatchedCount)
        {
            offline = std::to_wstring(offlineCount) + L" offline";
        }
        setText(LibraryDetailOfflineText(), offline);

        setText(LibraryDetailEditedText(), m_libraryDetailEditedText);
        setText(LibraryDetailDescriptionText(), m_libraryDetailDescription);

        auto dot = [](MUX::UIElement const& element, bool visible)
        {
            if (element)
            {
                element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
            }
        };
        dot(LibraryDetailMetaDot1(), !duration.empty());
        dot(LibraryDetailMetaDot2(), !offline.empty());
        dot(LibraryDetailMetaDot3(), !m_libraryDetailEditedText.empty());
    }

    // -------------------------------------------------------- view switching

    void MainWindow::SetLibraryDetailGridMode(bool gridMode)
    {
        m_libraryDetailGridMode = gridMode;
        EnsureAccentBrushes();

        if (auto listButton = LibraryDetailListViewButton())
        {
            listButton.Background(gridMode ? m_brushTransparent : m_brushAccentSoft);
        }
        if (auto gridButton = LibraryDetailGridViewButton())
        {
            gridButton.Background(gridMode ? m_brushAccentSoft : m_brushTransparent);
        }
        if (auto glyph = LibraryDetailListViewGlyph())
        {
            glyph.Foreground(gridMode ? m_brushGlyphIdle : m_brushAccent);
        }
        if (auto glyph = LibraryDetailGridViewGlyph())
        {
            glyph.Foreground(gridMode ? m_brushAccent : m_brushGlyphIdle);
        }

        if (auto surface = LibraryDetailListSurface())
        {
            surface.Visibility(gridMode ? Visibility::Collapsed : Visibility::Visible);
        }
        if (auto grid = LibraryDetailGridView())
        {
            grid.Visibility(gridMode ? Visibility::Visible : Visibility::Collapsed);
            if (gridMode && !grid.ItemsSource())
            {
                grid.ItemsSource(m_libraryDetailTracks);
            }
        }
        RefreshLibraryDetailRowStates();
    }

    void MainWindow::LibraryDetailListView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetLibraryDetailGridMode(false);
    }

    void MainWindow::LibraryDetailGridView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetLibraryDetailGridMode(true);
    }

    // ------------------------------------------------------- find and sort

    void MainWindow::LibraryDetailFind_TextChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::TextChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_libraryDetailFindSuppressed)
        {
            return;
        }

        auto const debounceId = ++m_libraryDetailFindDebounceId;
        auto const typed = LibraryDetailFindBox()
            ? std::wstring(detail::TrimQuery(LibraryDetailFindBox().Text()).c_str())
            : std::wstring{};

        [](winrt::weak_ref<MainWindow> weakThis,
           winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher,
           uint64_t debounceId,
           std::wstring typed) -> winrt::fire_and_forget
        {
            co_await winrt::resume_after(kFindDebounce);
            dispatcher.TryEnqueue([weakThis, debounceId, typed]()
            {
                auto self = weakThis.get();
                if (!self || debounceId != self->m_libraryDetailFindDebounceId)
                {
                    return;
                }
                if (self->m_libraryDetailFindText == typed)
                {
                    return;
                }
                self->m_libraryDetailFindText = typed;
                self->ReloadLibraryDetailTracks();
            });
        }(get_weak(), DispatcherQueue(), debounceId, std::move(typed));
    }

    void MainWindow::LibraryDetailSort_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<MUXC::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto const requested = LastMusicPlayer::Backend::ParseDetailSort(
            std::wstring(detail::ReadTagString(item.Tag()).c_str()),
            LibraryDetailSupportsCuratedOrder());
        if (requested == m_libraryDetailSort)
        {
            return;
        }

        m_libraryDetailSort = requested;
        if (auto label = LibraryDetailSortLabel())
        {
            label.Text(winrt::hstring(
                LastMusicPlayer::Backend::DetailSortLabel(m_libraryDetailSort)));
        }
        ReloadLibraryDetailTracks();
    }

    void MainWindow::ReloadLibraryDetailTracks()
    {
        if (m_libraryDetailKind.empty() || m_libraryDetailKey.empty())
        {
            return;
        }

        // Discards the current page set so hydration restarts at offset zero
        // under the new find and sort. Bumping both counters invalidates any
        // page request already in flight for the previous ordering.
        ClearLibraryDetailSelection();
        ++m_libraryDetailHydrationEpoch;
        ++m_libraryDetailPageLoadId;
        m_libraryDetailPageLoading = false;
        m_libraryDetailAllResults.clear();
        m_libraryDetailTracks.Clear();
        m_libraryDetailMatchedCount = 0;
        m_libraryDetailMatchedSeconds = 0.0;
        m_libraryDetailState = LoadState::Loading;

        if (m_libraryDetailKind == L"auto-playlist")
        {
            // Auto playlists are synthesized from m_homeMixes rather than
            // queried from SQLite. Re-entering the same detail applies the new
            // find/sort in ShowLibraryDetail's in-memory path.
            auto const title = LibraryDetailTitleText()
                ? LibraryDetailTitleText().Text()
                : winrt::hstring{ L"Playlist" };
            ShowLibraryDetail(
                L"auto-playlist",
                winrt::hstring(m_libraryDetailKey),
                title,
                winrt::hstring(m_libraryDetailSubtitle),
                m_libraryDetailFallbackArt);
            return;
        }

        if (auto empty = LibraryDetailEmptyFilterText())
        {
            empty.Visibility(Visibility::Collapsed);
        }
        detail::RunDetached(HydrateLibraryDetailTracksAsync());
    }

    void MainWindow::UpdateLibraryDetailEmptyState()
    {
        auto empty = LibraryDetailEmptyFilterText();
        if (!empty)
        {
            return;
        }

        auto const noRows = m_libraryDetailState == LoadState::Loaded
            && m_libraryDetailAllResults.empty();
        if (!noRows)
        {
            empty.Visibility(Visibility::Collapsed);
            return;
        }

        empty.Text(m_libraryDetailFindText.empty()
            ? winrt::hstring{ L"Nothing here yet." }
            : winrt::hstring(L"No tracks match “" + m_libraryDetailFindText + L"”"));
        empty.Visibility(Visibility::Visible);
    }

    // ------------------------------------------------------------ selection

    void MainWindow::ClearLibraryDetailSelection()
    {
        if (m_libraryDetailSelection.empty())
        {
            UpdateLibraryDetailSelectionBar();
            return;
        }
        m_libraryDetailSelection.clear();
        m_libraryDetailSelectionAnchor = -1;
        UpdateLibraryDetailSelectionBar();
        RefreshLibraryDetailRowStates();
    }

    void MainWindow::UpdateLibraryDetailSelectionBar()
    {
        auto bar = LibraryDetailSelectionBar();
        if (!bar)
        {
            return;
        }

        auto const count = m_libraryDetailSelection.size();
        bar.Visibility(count == 0 ? Visibility::Collapsed : Visibility::Visible);
        if (count == 0)
        {
            return;
        }
        if (auto text = LibraryDetailSelectionCountText())
        {
            text.Text(winrt::hstring(std::to_wstring(count) + L" selected"));
        }
    }

    std::vector<winrt::Last_Music_Player::TrackInfo> MainWindow::LibraryDetailSelectedTracks() const
    {
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        tracks.reserve(m_libraryDetailSelection.size());
        for (auto const index : m_libraryDetailSelection)
        {
            if (index < m_libraryDetailAllResults.size())
            {
                tracks.push_back(m_libraryDetailAllResults[index]);
            }
        }
        return tracks;
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::Last_Music_Player::TrackInfo>
        MainWindow::ChooseLibraryDetailMoveTargetAsync()
    {
        auto lifetime = get_strong();
        auto const accountSource = IsAccountPlaylistDetail();

        MUXC::ComboBox picker;
        picker.Header(winrt::box_value(winrt::hstring(L"Destination playlist")));
        picker.PlaceholderText(L"Choose a playlist");
        for (uint32_t index = 0; index < m_manualPlaylists.Size(); ++index)
        {
            auto const playlist = m_manualPlaylists.GetAt(index);
            if (!playlist || std::wstring(playlist.SourceUrl().c_str()) == m_libraryDetailKey)
            {
                continue;
            }
            auto const accountTarget = playlist.Provider() == L"account";
            if (accountTarget != accountSource)
            {
                continue;
            }

            MUXC::ComboBoxItem item;
            item.Content(winrt::box_value(playlist.Title()));
            item.Tag(playlist);
            picker.Items().Append(item);
        }

        if (picker.Items().Size() == 0)
        {
            LibraryImportStatusText().Text(accountSource
                ? L"Create another account playlist before moving tracks"
                : L"Create another playlist before moving tracks");
            co_return nullptr;
        }
        picker.SelectedIndex(0);

        MUXC::ContentDialog dialog;
        dialog.Title(winrt::box_value(winrt::hstring(L"Move selected tracks")));
        dialog.Content(picker);
        dialog.PrimaryButtonText(L"Move");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(MUXC::ContentDialogButton::Primary);
        dialog.XamlRoot(Content().XamlRoot());

        if (co_await dialog.ShowAsync() != MUXC::ContentDialogResult::Primary)
        {
            co_return nullptr;
        }
        auto const selected = picker.SelectedItem().try_as<MUXC::ComboBoxItem>();
        co_return selected
            ? selected.Tag().try_as<winrt::Last_Music_Player::TrackInfo>()
            : nullptr;
    }

    void MainWindow::ToggleLibraryDetailSelection(uint32_t index, bool extendRange)
    {
        if (index >= m_libraryDetailAllResults.size())
        {
            return;
        }

        if (extendRange && m_libraryDetailSelectionAnchor >= 0)
        {
            auto const anchor = static_cast<uint32_t>(m_libraryDetailSelectionAnchor);
            auto const first = (std::min)(anchor, index);
            auto const last = (std::max)(anchor, index);
            m_libraryDetailSelection.clear();
            for (auto position = first; position <= last; ++position)
            {
                m_libraryDetailSelection.push_back(position);
            }
        }
        else
        {
            auto const existing = std::find(
                m_libraryDetailSelection.begin(), m_libraryDetailSelection.end(), index);
            if (existing != m_libraryDetailSelection.end())
            {
                m_libraryDetailSelection.erase(existing);
            }
            else
            {
                m_libraryDetailSelection.push_back(index);
                std::sort(m_libraryDetailSelection.begin(), m_libraryDetailSelection.end());
            }
            m_libraryDetailSelectionAnchor = static_cast<int64_t>(index);
        }

        UpdateLibraryDetailSelectionBar();
        RefreshLibraryDetailRowStates();
    }

    // -------------------------------------------------------- row rendering

    void MainWindow::RefreshLibraryDetailRowStates()
    {
        auto const current = detail::AudioPlayerService().GetCurrentTrack();
        auto const currentKey = current ? detail::CatalogSourceKey(current) : std::wstring{};
        auto const currentInDetail = current && !currentKey.empty()
            && std::any_of(
                m_libraryDetailAllResults.begin(),
                m_libraryDetailAllResults.end(),
                [&currentKey](auto const& track)
                {
                    return track && detail::CatalogSourceKey(track) == currentKey;
                });
        auto const isPlaying = currentInDetail
            && ((m_sink == PlaybackSink::Cast && m_castSession.IsPlaying)
                || (m_sink == PlaybackSink::Local
                    && detail::AudioPlayerService().GetMediaPlayer().PlaybackSession().PlaybackState()
                        == winrt::Windows::Media::Playback::MediaPlaybackState::Playing));

        auto list = LibraryDetailTracksListView();
        auto grid = LibraryDetailGridView();
        for (uint32_t index = 0; index < m_libraryDetailTracks.Size(); ++index)
        {
            if (list)
            {
                auto container = list.ContainerFromIndex(static_cast<int32_t>(index))
                    .try_as<MUXC::ListViewItem>();
                if (container)
                {
                    ApplyLibraryDetailRowState(container, index);
                }
            }

            if (!grid)
            {
                continue;
            }
            auto container = grid.ContainerFromIndex(static_cast<int32_t>(index))
                .try_as<MUXC::GridViewItem>();
            auto root = container
                ? container.ContentTemplateRoot().try_as<MUXC::StackPanel>()
                : nullptr;
            if (!root || root.Children().Size() < 2)
            {
                continue;
            }

            auto const track = m_libraryDetailTracks.GetAt(index);
            auto const selected = std::find(
                m_libraryDetailSelection.begin(),
                m_libraryDetailSelection.end(),
                index) != m_libraryDetailSelection.end();
            auto const nowPlaying = track && !currentKey.empty()
                && detail::CatalogSourceKey(track) == currentKey;

            auto art = root.Children().GetAt(0).try_as<MUXC::Border>();
            if (art)
            {
                art.BorderBrush(selected ? m_brushAccent : m_brushTransparent);
                art.BorderThickness(selected ? MUX::Thickness{ 2 } : MUX::Thickness{ 0 });

                auto artGrid = art.Child().try_as<MUXC::Grid>();
                if (artGrid && artGrid.Children().Size() > 2)
                {
                    if (auto badge = artGrid.Children().GetAt(2).try_as<MUXC::Border>())
                    {
                        badge.Visibility(nowPlaying ? Visibility::Visible : Visibility::Collapsed);
                    }
                }
            }
            if (auto title = root.Children().GetAt(1).try_as<MUXC::TextBlock>())
            {
                title.Foreground(nowPlaying ? m_brushAccent : m_brushLabelIdle);
            }
        }

        if (auto glyph = LibraryDetailPlayGlyph())
        {
            glyph.Glyph(isPlaying ? L"\xE769" : L"\xE768");
        }
        if (auto label = LibraryDetailPlayLabel())
        {
            label.Text(isPlaying ? L"Pause" : L"Play");
        }
    }

    void MainWindow::SyncLibraryDetailPlaybackState()
    {
        if (LibraryDetailContent()
            && LibraryDetailContent().Visibility() == Visibility::Visible)
        {
            RefreshLibraryDetailRowStates();
        }
    }

    void MainWindow::ApplyLibraryDetailRowState(
        MUXC::ListViewItem const& container,
        uint32_t index)
    {
        if (!container || index >= m_libraryDetailTracks.Size())
        {
            return;
        }
        EnsureAccentBrushes();

        auto const track = m_libraryDetailTracks.GetAt(index);
        auto const selected = std::find(
            m_libraryDetailSelection.begin(),
            m_libraryDetailSelection.end(),
            index) != m_libraryDetailSelection.end();

        auto const current = detail::AudioPlayerService().GetCurrentTrack();
        auto const nowPlaying = track && current
            && detail::CatalogSourceKey(track) == detail::CatalogSourceKey(current);

        // Now playing wins over selection: it says what the app is doing,
        // while selection only says what the listener is about to act on.
        container.Background(nowPlaying
            ? m_brushAccentNowPlaying
            : (selected ? m_brushAccentSelection : m_brushTransparent));

        auto row = container.ContentTemplateRoot().try_as<MUXC::Grid>();
        if (!row || row.Children().Size() <= kRowPlayGlyphChild)
        {
            return;
        }
        if (auto indexText = row.Children().GetAt(kRowIndexTextChild).try_as<MUXC::TextBlock>())
        {
            indexText.Visibility(nowPlaying ? Visibility::Collapsed : Visibility::Visible);
        }
        if (auto playGlyph = row.Children().GetAt(kRowPlayGlyphChild).try_as<MUXC::FontIcon>())
        {
            playGlyph.Visibility(nowPlaying ? Visibility::Visible : Visibility::Collapsed);
        }
        if (auto title = RowTitleTextBlock(row))
        {
            title.Foreground(nowPlaying ? m_brushAccent : m_brushLabelIdle);
        }
    }

    void MainWindow::LibraryDetailGrid_ContainerContentChanging(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        if (!args.InRecycleQueue())
        {
            MaybeAppendLibraryDetailPage(args.ItemIndex());
        }
    }

    // -------------------------------------------------------------- actions

    void MainWindow::LibraryDetailTrack_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }

        uint32_t index = 0;
        if (!m_libraryDetailTracks.IndexOf(track, index))
        {
            return;
        }

        // The prototype makes a bare click select. Every other list in this app
        // plays on click, so the modifier keys carry selection instead: that
        // keeps the detail page consistent with the Songs, History and search
        // lists, and matches how a Windows list behaves everywhere else.
        auto const modifiers = winrt::Microsoft::UI::Input::InputKeyboardSource
            ::GetKeyStateForCurrentThread(winrt::Windows::System::VirtualKey::Control);
        auto const shift = winrt::Microsoft::UI::Input::InputKeyboardSource
            ::GetKeyStateForCurrentThread(winrt::Windows::System::VirtualKey::Shift);
        auto const held = [](winrt::Windows::UI::Core::CoreVirtualKeyStates state)
        {
            return (state & winrt::Windows::UI::Core::CoreVirtualKeyStates::Down)
                == winrt::Windows::UI::Core::CoreVirtualKeyStates::Down;
        };

        if (held(modifiers) || held(shift))
        {
            ToggleLibraryDetailSelection(index, held(shift));
            return;
        }

        ClearLibraryDetailSelection();
        detail::RunDetached(LoadLibraryDetailQueueAndPlayAsync(track));
    }

    void MainWindow::LibraryDetailRowLike_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        ToggleTrackLiked(TrackFromActionSender(sender));
    }

    void MainWindow::LibraryDetailSelectionAddToQueue_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto const tracks = LibraryDetailSelectedTracks();
        if (tracks.empty())
        {
            return;
        }
        AddSongsTracksToQueueBulk(tracks);
        ClearLibraryDetailSelection();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryDetailSelectionMove_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto const tracks = LibraryDetailSelectedTracks();
        if (tracks.empty() || !IsLibraryDetailPlaylist() || !detail::DatabaseService().IsInitialized())
        {
            co_return;
        }

        auto accountContext = IsAccountPlaylistDetail()
            ? CaptureAccountPlaylistDetailContext()
            : std::optional<AccountPlaylistDetailContext>{};
        if (IsAccountPlaylistDetail() && !accountContext)
        {
            LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
            co_return;
        }

        auto const target = co_await ChooseLibraryDetailMoveTargetAsync();
        if (!target)
        {
            co_return;
        }

        if (accountContext)
        {
            auto const targetBinding = AccountPlaylistBindingFor(target);
            if (!targetBinding
                || targetBinding->OwnerId != accountContext->Binding.OwnerId
                || !IsCurrentAccountPlaylistBinding(*targetBinding))
            {
                LibraryImportStatusText().Text(L"The destination playlist changed. Refresh and try again.");
                co_return;
            }

            std::unordered_set<std::wstring> selectedIds;
            for (auto const& track : tracks)
            {
                if (track && !track.RemoteId().empty())
                {
                    selectedIds.emplace(track.RemoteId().c_str());
                }
            }
            if (selectedIds.empty())
            {
                co_return;
            }

            auto dispatcher = DispatcherQueue();
            auto const targetPlaylistId = std::wstring(target.RemoteId().c_str());
            auto const targetTitle = target.Title();
            auto const sourceTitle = LibraryDetailTitleText().Text();
            co_await winrt::resume_background();
            auto const sourceTracks = detail::DatabaseService().LoadAccountPlaylistTracks(
                accountContext->Binding.OwnerId,
                accountContext->PlaylistId);
            auto const targetTracks = detail::DatabaseService().LoadAccountPlaylistTracks(
                targetBinding->OwnerId,
                targetPlaylistId);

            std::vector<winrt::hstring> sourceRemoteIds;
            sourceRemoteIds.reserve(sourceTracks.size());
            for (auto const& track : sourceTracks)
            {
                if (!track.RemoteId().empty() && !selectedIds.contains(track.RemoteId().c_str()))
                {
                    sourceRemoteIds.push_back(track.RemoteId());
                }
            }

            std::vector<winrt::hstring> targetRemoteIds;
            targetRemoteIds.reserve(targetTracks.size() + selectedIds.size());
            std::unordered_set<std::wstring> targetIds;
            for (auto const& track : targetTracks)
            {
                if (!track.RemoteId().empty() && targetIds.emplace(track.RemoteId().c_str()).second)
                {
                    targetRemoteIds.push_back(track.RemoteId());
                }
            }
            for (auto const& track : tracks)
            {
                if (track && !track.RemoteId().empty()
                    && targetIds.emplace(track.RemoteId().c_str()).second)
                {
                    targetRemoteIds.push_back(track.RemoteId());
                }
            }
            auto const targetBody = detail::BuildAccountPlaylistUpdateJson(targetTitle, targetRemoteIds);
            auto const sourceBody = detail::BuildAccountPlaylistUpdateJson(sourceTitle, sourceRemoteIds);
            co_await wil::resume_foreground(dispatcher);

            if (!IsCurrentAccountPlaylistDetailContext(*accountContext)
                || !IsCurrentAccountPlaylistBinding(*targetBinding))
            {
                co_return;
            }

            auto operationLease = detail::UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }

            auto targetUpdated = false;
            try
            {
                co_await detail::RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    targetBinding->Scope,
                    target.RemoteId(),
                    targetBody);
                targetUpdated = true;
                co_await detail::RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    accountContext->Binding.Scope,
                    winrt::hstring(accountContext->PlaylistId),
                    sourceBody);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                if (IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    ClearLibraryDetailSelection();
                    ShowLibraryDetail(
                        L"playlist",
                        winrt::hstring(accountContext->DetailKey),
                        sourceTitle,
                        winrt::hstring(m_libraryDetailSubtitle));
                    LibraryImportStatusText().Text(L"Moved selected tracks");
                }
            }
            catch (...)
            {
                operationLease.reset();
                LibraryImportStatusText().Text(targetUpdated
                    ? L"Tracks reached the destination, but could not be removed from this playlist"
                    : L"Could not move the selected tracks");
            }
            co_return;
        }

        std::vector<int64_t> trackIds;
        trackIds.reserve(tracks.size());
        for (auto const& track : tracks)
        {
            if (track && track.CatalogId() > 0)
            {
                trackIds.push_back(track.CatalogId());
            }
        }
        if (trackIds.empty())
        {
            co_return;
        }

        auto operationLease = detail::UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto const sourceKey = m_libraryDetailKey;
        auto const targetKey = std::wstring(target.SourceUrl().c_str());
        auto const detailEpoch = m_libraryDetailHydrationEpoch;
        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();
        auto const moved = detail::DatabaseService().MoveTracksBetweenPlaylists(
            sourceKey,
            targetKey,
            trackIds);
        co_await wil::resume_foreground(dispatcher);
        operationLease.reset();

        if (!moved)
        {
            LibraryImportStatusText().Text(L"Could not move the selected tracks");
            co_return;
        }
        if (detailEpoch != m_libraryDetailHydrationEpoch || m_libraryDetailKey != sourceKey)
        {
            co_return;
        }

        ClearLibraryDetailSelection();
        MarkLibraryViewsDirty();
        ReloadLibraryDetailTracks();
        LibraryImportStatusText().Text(L"Moved selected tracks");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryDetailSelectionRemove_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        if (!IsLibraryDetailPlaylist() || !detail::DatabaseService().IsInitialized())
        {
            co_return;
        }

        auto const tracks = LibraryDetailSelectedTracks();
        if (tracks.empty())
        {
            co_return;
        }

        if (IsAccountPlaylistDetail())
        {
            auto const accountContext = CaptureAccountPlaylistDetailContext();
            if (!accountContext)
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }

            std::unordered_set<std::wstring> selectedIds;
            for (auto const& track : tracks)
            {
                if (track && !track.RemoteId().empty())
                {
                    selectedIds.emplace(track.RemoteId().c_str());
                }
            }
            if (selectedIds.empty())
            {
                co_return;
            }

            auto dispatcher = DispatcherQueue();
            auto const title = LibraryDetailTitleText().Text();
            auto const subtitle = m_libraryDetailSubtitle;
            co_await winrt::resume_background();
            auto const currentTracks = detail::DatabaseService().LoadAccountPlaylistTracks(
                accountContext->Binding.OwnerId,
                accountContext->PlaylistId);
            std::vector<winrt::hstring> remainingIds;
            remainingIds.reserve(currentTracks.size());
            for (auto const& track : currentTracks)
            {
                if (!track.RemoteId().empty() && !selectedIds.contains(track.RemoteId().c_str()))
                {
                    remainingIds.push_back(track.RemoteId());
                }
            }
            auto const body = detail::BuildAccountPlaylistUpdateJson(title, remainingIds);
            co_await wil::resume_foreground(dispatcher);
            if (!IsCurrentAccountPlaylistDetailContext(*accountContext))
            {
                co_return;
            }

            auto operationLease = detail::UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            try
            {
                co_await detail::RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    accountContext->Binding.Scope,
                    winrt::hstring(accountContext->PlaylistId),
                    body);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                if (IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    ClearLibraryDetailSelection();
                    ShowLibraryDetail(
                        L"playlist",
                        winrt::hstring(accountContext->DetailKey),
                        title,
                        winrt::hstring(subtitle));
                    LibraryImportStatusText().Text(L"Removed selected tracks");
                }
            }
            catch (...)
            {
                LibraryImportStatusText().Text(L"Could not update the account playlist");
            }
            co_return;
        }

        std::vector<int64_t> trackIds;
        trackIds.reserve(tracks.size());
        for (auto const& track : tracks)
        {
            if (track && track.CatalogId() > 0)
            {
                trackIds.push_back(track.CatalogId());
            }
        }
        if (trackIds.empty())
        {
            co_return;
        }

        auto operationLease = detail::UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto const sourceKey = m_libraryDetailKey;
        auto const detailEpoch = m_libraryDetailHydrationEpoch;
        auto dispatcher = DispatcherQueue();
        co_await winrt::resume_background();
        auto const removed = detail::DatabaseService().RemoveTracksFromPlaylist(sourceKey, trackIds);
        co_await wil::resume_foreground(dispatcher);
        operationLease.reset();
        if (!removed)
        {
            LibraryImportStatusText().Text(L"Could not remove the selected tracks");
            co_return;
        }
        if (detailEpoch != m_libraryDetailHydrationEpoch || m_libraryDetailKey != sourceKey)
        {
            co_return;
        }
        ClearLibraryDetailSelection();
        MarkLibraryViewsDirty();
        ReloadLibraryDetailTracks();
        LibraryImportStatusText().Text(L"Removed selected tracks");
    }

    // ------------------------------------------------------ collection menu

    void MainWindow::LibraryDetailMoreMenu_Opening(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Windows::Foundation::IInspectable const& args)
    {
        (void)sender;
        (void)args;
        ApplyLibraryDetailKindAffordances();
    }

    void MainWindow::LibraryDetailMenuPlayNext_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        PlayNextFromSongsBulk(m_libraryDetailAllResults);
    }

    void MainWindow::LibraryDetailMenuAddToQueue_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        AddSongsTracksToQueueBulk(m_libraryDetailAllResults);
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::CurrentLibraryDetailPlaylistGroup()
    {
        if (!IsLibraryDetailPlaylist())
        {
            return nullptr;
        }
        for (uint32_t index = 0; index < m_manualPlaylists.Size(); ++index)
        {
            auto const playlist = m_manualPlaylists.GetAt(index);
            if (playlist && std::wstring(playlist.SourceUrl().c_str()) == m_libraryDetailKey)
            {
                return playlist;
            }
        }
        return nullptr;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryDetailEdit_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        auto lifetime = get_strong();
        auto playlist = CurrentLibraryDetailPlaylistGroup();
        if (!playlist)
        {
            co_return;
        }

        // The rename and delete dialogs read their subject off the sender's
        // Tag, so the open playlist is handed over the same way a card would.
        MUXC::Button proxy;
        proxy.Tag(playlist);
        co_await LibraryPlaylistRename_Click(proxy, args);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryDetailMenuDelete_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        auto lifetime = get_strong();
        auto playlist = CurrentLibraryDetailPlaylistGroup();
        if (!playlist)
        {
            co_return;
        }

        MUXC::Button proxy;
        proxy.Tag(playlist);
        co_await LibraryPlaylistDelete_Click(proxy, args);
    }

    // ---------------------------------------------------------- suggestions

    void MainWindow::LibraryDetailSuggestionAdd_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto track = TrackFromActionSender(sender);
        if (!track || !IsLibraryDetailPlaylist() || !detail::DatabaseService().IsInitialized())
        {
            return;
        }
        if (track.CatalogId() == 0)
        {
            return;
        }
        if (!detail::DatabaseService().AddTrackToPlaylist(m_libraryDetailKey, track.CatalogId()))
        {
            return;
        }

        MarkLibraryViewsDirty();
        ReloadLibraryDetailTracks();
        detail::RunDetached(HydrateLibraryDetailSuggestionsAsync());
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateLibraryDetailSuggestionsAsync()
    {
        auto lifetime = get_strong();
        auto panel = LibraryDetailSuggestionsPanel();
        if (!panel)
        {
            co_return;
        }

        // Account-backed playlists are not stored in the local Playlists table,
        // so there is no row to add a local library track to.
        if (!IsLibraryDetailPlaylist()
            || IsAccountPlaylistDetail()
            || !detail::DatabaseService().IsInitialized())
        {
            panel.Visibility(Visibility::Collapsed);
            m_libraryDetailSuggestions.Clear();
            co_return;
        }

        auto const epoch = ++m_libraryDetailSuggestionEpoch;
        auto const playlistKey = m_libraryDetailKey;
        auto dispatcher = DispatcherQueue();

        co_await winrt::resume_background();

        auto const members = detail::DatabaseService().LoadTracksForGroup(L"playlist", playlistKey);
        auto const library = detail::DatabaseService().LoadTracks(true, true);
        auto const stats = detail::DatabaseService().LoadPlaybackStats();

        std::unordered_map<std::wstring, uint32_t> playCounts;
        playCounts.reserve(stats.size());
        for (auto const& stat : stats)
        {
            playCounts.emplace(stat.SourceKey, stat.PlayCount);
        }

        LastMusicPlayer::Backend::SuggestionContext context;
        context.MemberKeys.reserve(members.size());
        context.MemberArtists.reserve(members.size());
        context.MemberGenres.reserve(members.size());
        for (auto const& member : members)
        {
            if (!member)
            {
                continue;
            }
            context.MemberKeys.push_back(detail::CatalogSourceKey(member));
            context.MemberArtists.push_back(FoldedText(member.Artist()));
            context.MemberGenres.push_back(FoldedText(member.Genre()));
        }

        std::vector<LastMusicPlayer::Backend::SuggestionCandidate> candidates;
        std::vector<winrt::Last_Music_Player::TrackInfo> candidateTracks;
        candidates.reserve(library.size());
        candidateTracks.reserve(library.size());
        for (auto const& track : library)
        {
            if (!track)
            {
                continue;
            }
            auto key = detail::CatalogSourceKey(track);
            auto const playCount = playCounts.find(key);
            candidates.push_back({
                key,
                FoldedText(track.Artist()),
                FoldedText(track.Genre()),
                playCount == playCounts.end() ? 0u : playCount->second
            });
            candidateTracks.push_back(track);
        }

        auto const ranked = LastMusicPlayer::Backend::RankPlaylistSuggestions(
            candidates, context, kSuggestionLimit);

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_libraryDetailSuggestionEpoch || m_libraryDetailKey != playlistKey)
        {
            co_return;
        }

        m_libraryDetailSuggestions.Clear();
        for (auto const index : ranked)
        {
            auto track = candidateTracks[index];
            detail::ResolveArtworkPresentation(track, L"track");
            m_libraryDetailSuggestions.Append(track);
        }
        panel.Visibility(m_libraryDetailSuggestions.Size() == 0
            ? Visibility::Collapsed
            : Visibility::Visible);
    }
}
