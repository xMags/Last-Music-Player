#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/BrowseGroupingPolicy.h"
#include "Backend/RecentSearchStore.h"
#include "Backend/TrackSearchPolicy.h"
#include "Frontend/BrowseResultItem.h"
#include "Frontend/ResponsiveCardLayout.h"

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.System.h>

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;
        using MUX::Visibility;

        constexpr wchar_t kBrowseViewSetting[] = L"BrowseResultView";

        winrt::hstring TagText(winrt::Windows::Foundation::IInspectable const& value)
        {
            try { return winrt::unbox_value_or<winrt::hstring>(value, L""); }
            catch (...) { return {}; }
        }

        winrt::hstring KindTag(LastMusicPlayer::Backend::BrowseResultKind kind)
        {
            using LastMusicPlayer::Backend::BrowseResultKind;
            switch (kind)
            {
            case BrowseResultKind::Song: return L"song";
            case BrowseResultKind::Album: return L"album";
            case BrowseResultKind::Playlist: return L"playlist";
            case BrowseResultKind::Artist: return L"artist";
            }
            return {};
        }

        winrt::hstring ResultCountText(std::size_t count)
        {
            return winrt::hstring(std::to_wstring(count) + (count == 1 ? L" result" : L" results"));
        }

        template <typename T>
        T FindBrowseDescendant(MUX::DependencyObject const& root)
        {
            if (!root)
            {
                return nullptr;
            }
            if (auto match = root.try_as<T>())
            {
                return match;
            }
            auto const count = MUX::Media::VisualTreeHelper::GetChildrenCount(root);
            for (int32_t index = 0; index < count; ++index)
            {
                auto child = MUX::Media::VisualTreeHelper::GetChild(root, index);
                if (auto found = FindBrowseDescendant<T>(child))
                {
                    return found;
                }
            }
            return nullptr;
        }
    }

    void MainWindow::BrowseButton_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ShowPrimaryView(L"Browse");
        auto const query = TrimQuery(GlobalSearchBox().Text());
        if (query.empty())
        {
            ShowBrowseLanding();
        }
        else if (!m_searchAllResults.empty() || !m_searchMatchingPlaylists.empty())
        {
            ShowBrowseSearchResults(query);
        }
        else
        {
            RunDetached(RunHomeSearchAsync());
        }
        RunDetached(HydrateBrowseLandingAsync(false));
    }

    void MainWindow::HomeSearchEntry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        BrowseButton_Click(sender, args);
        GlobalSearchBox().Focus(MUX::FocusState::Programmatic);
    }

    void MainWindow::HomeSearchEntry_Tapped(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::TappedRoutedEventArgs const& args)
    {
        (void)sender;
        BrowseButton_Click(nullptr, nullptr);
        GlobalSearchBox().Focus(MUX::FocusState::Programmatic);
        args.Handled(true);
    }

    void MainWindow::BrowseResultsContent_SizeChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::SizeChangedEventArgs const& args)
    {
        auto scrollViewer = sender.try_as<MUXC::ScrollViewer>();
        auto viewportContent = BrowseResultsViewport();
        if (!scrollViewer || !viewportContent)
        {
            return;
        }

        auto width = scrollViewer.ViewportWidth();
        if (!std::isfinite(width) || width <= 0.0)
        {
            width = args.NewSize().Width;
        }
        if (!std::isfinite(width) || width <= 0.0)
        {
            return;
        }

        auto const currentWidth = viewportContent.Width();
        if (std::isnan(currentWidth) || std::abs(currentWidth - width) > 0.5)
        {
            viewportContent.Width(width);
        }
    }

    void MainWindow::BrowseCategory_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto category = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!category || category.Title().empty())
        {
            return;
        }

        if (category.SourceKind() == L"catalog-search")
        {
            GlobalSearchBox().Text(category.SourceUrl().empty()
                ? category.Title()
                : category.SourceUrl());
            RunDetached(RunHomeSearchAsync());
            return;
        }

        auto const kind = winrt::hstring{ L"genre" };
        auto const tab = LibTabGenres();
        ShowPrimaryView(L"Library");
        if (tab)
        {
            tab.IsChecked(true);
            LibraryTab_Checked(tab, nullptr);
        }
        ShowLibraryDetail(
            kind,
            category.SourceUrl().empty() ? category.Title() : category.SourceUrl(),
            category.Title(),
            {},
            ApprovedDetailArtwork(category, kind),
            category);
    }

    void MainWindow::BrowseCategory_ContainerContentChanging(
        MUXC::ListViewBase const& sender,
        MUXC::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        if (args.InRecycleQueue() || !args.ItemContainer())
        {
            return;
        }

        ApplyBrowseCategoryCardSize(args.ItemContainer());
    }

    void MainWindow::ApplyBrowseCategoryCardSize(
        MUXC::Primitives::SelectorItem const& container)
    {
        auto panel = BrowseCategoryGrid().ItemsPanelRoot().try_as<MUXC::ItemsWrapGrid>();
        auto root = container ? container.ContentTemplateRoot().try_as<MUXC::Grid>() : nullptr;
        constexpr double gap = LastMusicPlayer::Frontend::kBannerCardGap;
        auto const cardWidth = panel ? panel.ItemWidth() - gap : 0.0;
        if (!root || cardWidth <= 0.0)
        {
            return;
        }
        root.Width(cardWidth);
        root.Height(cardWidth / 1.5);
    }

    void MainWindow::BrowseClearQuery_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_browseResultMenu)
        {
            m_browseResultMenu.Hide();
        }
        GlobalSearchBox().Text(L"");
        GlobalSearchBox().Focus(MUX::FocusState::Programmatic);
    }

    void MainWindow::BrowseClearRecent_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        m_recentSearches.clear();
        SettingsManagerService().SetString(L"RecentSearches", L"");
        RebuildBrowseRecentChips();
    }

    void MainWindow::BrowseRecentSearch_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const query = element ? TagText(element.Tag()) : winrt::hstring{};
        if (query.empty())
        {
            return;
        }
        GlobalSearchBox().Text(query);
        RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::BrowseResult_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto item = args.ClickedItem().try_as<winrt::Last_Music_Player::BrowseResultItem>();
        if (!item)
        {
            return;
        }
        RecordBrowseRecentSearch(TrimQuery(GlobalSearchBox().Text()));
        if (item.Kind() == L"artist")
        {
            auto representative = winrt::get_self<BrowseResultItem>(item)->RepresentativeTrack();
            OpenArtistDetail(
                item.Title(),
                representative ? representative.ArtistCatalogId() : winrt::hstring{},
                {},
                item.ArtworkUrl(),
                representative);
            return;
        }
        RunDetached(PlayBrowseResultAsync(item, false));
    }

    void MainWindow::BrowseResult_RightTapped(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::RightTappedRoutedEventArgs const& args)
    {
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto item = element ? element.Tag().try_as<winrt::Last_Music_Player::BrowseResultItem>() : nullptr;
        if (!element || !item)
        {
            return;
        }

        if (m_browseResultMenu)
        {
            m_browseResultMenu.Hide();
        }
        MUXC::MenuFlyout menu;
        MUXC::MenuFlyoutItem header;
        header.Text(item.Title());
        header.IsEnabled(false);
        header.MinWidth(238.0);
        header.MinHeight(34.0);
        header.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
        menu.Items().Append(header);
        menu.Items().Append(MUXC::MenuFlyoutSeparator{});

        auto addAction = [this, &menu, item](
            winrt::hstring const& text,
            winrt::hstring const& glyph,
            winrt::hstring const& action)
        {
            MUXC::MenuFlyoutItem menuItem;
            menuItem.Text(text);
            menuItem.MinHeight(34.0);
            MUXC::FontIcon icon;
            icon.Glyph(glyph);
            icon.FontSize(14);
            menuItem.Icon(icon);
            menuItem.Click([weakThis = get_weak(), item, action](auto&&, auto&&)
            {
                if (auto self = weakThis.get())
                {
                    RunDetached(self->RunBrowseResultActionAsync(item, action));
                }
            });
            menu.Items().Append(menuItem);
        };

        auto const kind = item.Kind();
        if (kind == L"song")
        {
            addAction(L"Play", L"\xE768", L"play");
            addAction(L"Play next", L"\xE893", L"next");
            addAction(L"Add to queue", L"\xE710", L"queue");
            addAction(L"Add to playlist", L"\xE728", L"playlist");
            auto representative = winrt::get_self<BrowseResultItem>(item)->RepresentativeTrack();
            addAction(representative && representative.IsLiked()
                ? L"Remove from Liked Songs"
                : L"Add to Liked Songs", L"\xEB51", L"like");
        }
        else if (kind == L"album")
        {
            addAction(L"Play album", L"\xE768", L"play");
            addAction(L"Shuffle album", L"\xE8B1", L"shuffle");
            addAction(L"Add to queue", L"\xE710", L"queue");
            addAction(L"Add to playlist", L"\xE728", L"playlist");
        }
        else if (kind == L"artist")
        {
            addAction(L"Play artist", L"\xE768", L"play");
            addAction(L"Shuffle artist", L"\xE8B1", L"shuffle");
            addAction(L"Add to queue", L"\xE710", L"queue");
        }
        else if (kind == L"playlist")
        {
            addAction(L"Play playlist", L"\xE768", L"play");
            addAction(L"Shuffle playlist", L"\xE8B1", L"shuffle");
            addAction(L"Add to queue", L"\xE710", L"queue");
            auto representative = winrt::get_self<BrowseResultItem>(item)->RepresentativeTrack();
            if (representative && representative.SourceKind() == L"playlist")
            {
                addAction(L"Rename", L"\xE8AC", L"rename");
            }
        }

        auto const& members = winrt::get_self<BrowseResultItem>(item)->MemberTracks();
        auto const hasLocalMember = std::any_of(members.begin(), members.end(), [](auto const& track)
        {
            return track && (track.File()
                || (!track.FilePath().empty() && !IsHttpUrl(track.FilePath())));
        });
        if ((kind == L"song" || kind == L"album") && hasLocalMember)
        {
            addAction(L"Show in folder", L"\xE8B7", L"folder");
        }

        m_browseResultMenu = menu;
        winrt::Microsoft::UI::Xaml::Controls::Primitives::FlyoutShowOptions options;
        options.Position(args.GetPosition(element));
        menu.ShowAt(element, options);
        args.Handled(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::PlayBrowseResultAsync(
        winrt::Last_Music_Player::BrowseResultItem item,
        bool shuffle)
    {
        co_await RunBrowseResultActionAsync(item, shuffle ? L"shuffle" : L"play");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RunBrowseResultActionAsync(
        winrt::Last_Music_Player::BrowseResultItem item,
        winrt::hstring action)
    {
        if (!item)
        {
            co_return;
        }

        auto itemImplementation = winrt::get_self<BrowseResultItem>(item);
        if (!itemImplementation->RepresentativeTrack())
        {
            co_return;
        }
        auto representative = itemImplementation->RepresentativeTrack();
        if (action == L"like")
        {
            ToggleTrackLiked(representative);
            co_return;
        }
        if (action == L"rename")
        {
            MUXC::Button proxy;
            proxy.Tag(representative);
            co_await LibraryPlaylistRename_Click(proxy, nullptr);
            if (!TrimQuery(GlobalSearchBox().Text()).empty())
            {
                co_await RunHomeSearchAsync();
            }
            co_return;
        }

        auto const kind = std::wstring(item.Kind().c_str());
        auto const key = std::wstring(item.Key().c_str());
        auto tracks = itemImplementation->MemberTracks();
        std::wstring accountOwner;
        if (key.starts_with(L"account-playlist|"))
        {
            try
            {
                auto context = RemoteMusicServiceService().CaptureAccountSyncContext();
                accountOwner.assign(context.OwnerId().c_str());
            }
            catch (...)
            {
            }
        }
        if (kind != L"song" && DatabaseService().IsInitialized() && !key.empty())
        {
            LastMusicPlayer::Backend::TrackQuery query;
            query.GroupKind = kind;
            query.GroupKey = key;
            query.Sort = kind == L"playlist" ? L"" : L"DateAdded";
            query.IncludeRemote = true;
            query.ActiveOnly = true;
            query.AccountOwnerId = std::move(accountOwner);

            auto dispatcher = DispatcherQueue();
            co_await winrt::resume_background();
            auto complete = DatabaseService().LoadTrackPage(query).Tracks;
            co_await wil::resume_foreground(dispatcher);
            if (!complete.empty())
            {
                if (kind == L"album" && !representative.Artist().empty())
                {
                    auto const expectedArtist = ToLowerCopy(representative.Artist());
                    std::erase_if(complete, [&](auto const& track)
                    {
                        return ToLowerCopy(track.Artist()) != expectedArtist;
                    });
                }
                if (!complete.empty())
                {
                    tracks = std::move(complete);
                }
            }
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> playable;
        playable.reserve(tracks.size());
        std::unordered_set<std::wstring> seen;
        for (auto const& track : tracks)
        {
            if (!track || !IsPlayableHomeTrack(track))
            {
                continue;
            }
            auto identity = HomeQueueDedupeKey(track);
            if (seen.insert(identity).second)
            {
                playable.push_back(track);
            }
        }
        if (playable.empty())
        {
            ShowPlaybackNotice(L"No playable tracks are available for this result.");
            co_return;
        }
        if (action == L"playlist")
        {
            co_await AddTracksToPlaylistAsync(playable);
            co_return;
        }
        if (action == L"folder")
        {
            for (auto const& track : playable)
            {
                auto file = track.File();
                try
                {
                    if (!file && !track.FilePath().empty() && !IsHttpUrl(track.FilePath()))
                    {
                        file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(track.FilePath());
                    }
                    if (!file)
                    {
                        continue;
                    }
                    auto folder = co_await file.GetParentAsync();
                    if (!folder)
                    {
                        continue;
                    }
                    winrt::Windows::System::FolderLauncherOptions launchOptions;
                    launchOptions.ItemsToSelect().Append(file);
                    (void)co_await winrt::Windows::System::Launcher::LaunchFolderAsync(folder, launchOptions);
                    co_return;
                }
                catch (...)
                {
                }
            }
            ShowPlaybackNotice(L"The local file could not be opened in File Explorer.");
            co_return;
        }
        if (action == L"queue")
        {
            AddSongsTracksToQueueBulk(playable);
            co_return;
        }
        if (action == L"next")
        {
            if (!AudioPlayerService().GetCurrentTrack())
            {
                SetPlaybackQueue(playable, 0);
                PlayTrack(playable.front());
            }
            else
            {
                PlayNextFromSongsBulk(playable);
            }
            co_return;
        }
        if (action == L"shuffle" && playable.size() > 1)
        {
            std::random_device randomDevice;
            std::mt19937 generator(randomDevice());
            std::shuffle(playable.begin(), playable.end(), generator);
        }
        SetPlaybackQueue(playable, 0);
        PlayTrack(playable.front());
    }

    void MainWindow::BrowseResultArtwork_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto image = sender.try_as<MUXC::Image>();
        auto item = image ? image.DataContext().try_as<winrt::Last_Music_Player::BrowseResultItem>() : nullptr;
        if (!image || !item)
        {
            return;
        }
        auto representative = winrt::get_self<BrowseResultItem>(item)->RepresentativeTrack();
        if (!representative)
        {
            return;
        }
        // Loaded means this template is realized now. Promote it directly
        // instead of waiting for a future viewport-change notification that an
        // already-visible element may never raise. Image.Tag is reserved for
        // the request-generation token written by QueueAccountArtworkImage.
        QueueAccountArtworkImage(
            image,
            item.ArtworkUrl(),
            ArtworkDetail::Tile,
            representative,
            AccountArtworkPriority::Visible);
    }

    void MainWindow::BrowseResult_ContainerContentChanging(
        MUXC::ListViewBase const& sender,
        MUXC::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        if (args.InRecycleQueue() || !args.ItemContainer())
        {
            return;
        }

        auto item = args.Item().try_as<winrt::Last_Music_Player::BrowseResultItem>();
        auto root = args.ItemContainer().ContentTemplateRoot();
        auto image = FindBrowseDescendant<MUXC::Image>(root);
        if (!item || !image)
        {
            return;
        }

        auto representative = winrt::get_self<BrowseResultItem>(item)->RepresentativeTrack();
        if (!representative)
        {
            return;
        }

        // Loaded runs only for a newly created template. This handler also runs
        // when WinUI recycles that template onto another result, so the image's
        // generation token and request are always replaced for the new item.
        QueueAccountArtworkImage(
            image,
            item.ArtworkUrl(),
            ArtworkDetail::Tile,
            representative);
    }

    void MainWindow::SearchListView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSearchGridMode(false);
    }

    void MainWindow::SearchGridView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSearchGridMode(true);
    }

    void MainWindow::SearchRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::ShowBrowseLanding()
    {
        BrowseLandingPanel().Visibility(Visibility::Visible);
        BrowseSearchPanel().Visibility(Visibility::Collapsed);
        BrowseClearQueryButton().Visibility(GlobalSearchBox().Text().empty()
            ? Visibility::Collapsed
            : Visibility::Visible);
        m_searchSkeleton.EndLoading();
        RebuildBrowseRecentChips();
    }

    void MainWindow::ShowBrowseSearchLoading()
    {
        auto const query = std::wstring(TrimQuery(GlobalSearchBox().Text()).c_str());
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchStatusText().Text(winrt::hstring(L"Results for “" + query + L"”"));
        SearchResultCountText().Text(L"");
        SearchLoadingText().Visibility(Visibility::Visible);
        SearchErrorPanel().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        BrowseGroupedResultsPanel().Visibility(Visibility::Collapsed);
        ApplyBrowseViewVisuals();
    }

    void MainWindow::ShowBrowseSearchResults(winrt::hstring const& query)
    {
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Collapsed);
        SearchStatusText().Text(winrt::hstring(L"Results for “" + std::wstring(query.c_str()) + L"”"));
        RebuildBrowseResults();
    }

    void MainWindow::ShowBrowseSearchError(winrt::hstring const& message)
    {
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        BrowseGroupedResultsPanel().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Visible);
        SearchErrorMessageText().Text(message);
    }

    void MainWindow::SetSearchGridMode(bool gridMode)
    {
        if (m_searchGridMode != gridMode)
        {
            m_searchGridMode = gridMode;
            auto const preference = gridMode
                ? LastMusicPlayer::Backend::BrowseViewPreference::Grid
                : LastMusicPlayer::Backend::BrowseViewPreference::List;
            SettingsManagerService().SetString(
                kBrowseViewSetting,
                winrt::hstring(LastMusicPlayer::Backend::BrowseViewPreferenceTag(preference)));
        }
        ApplyBrowseViewVisuals();
    }

    void MainWindow::LoadPersistedBrowseView()
    {
        auto const saved = std::wstring(SettingsManagerService().GetString(kBrowseViewSetting, L"grid").c_str());
        m_searchGridMode = LastMusicPlayer::Backend::ParseBrowseViewPreference(saved)
            == LastMusicPlayer::Backend::BrowseViewPreference::Grid;
        ApplyBrowseViewVisuals();
    }

    void MainWindow::ApplyBrowseViewVisuals()
    {
        auto const gridVisibility = m_searchGridMode ? Visibility::Visible : Visibility::Collapsed;
        auto const listVisibility = m_searchGridMode ? Visibility::Collapsed : Visibility::Visible;
        for (auto const& grid : { BrowseSongsGrid(), BrowseAlbumsGrid(), BrowsePlaylistsGrid(), BrowseArtistsGrid() })
        {
            grid.Visibility(gridVisibility);
        }
        for (auto const& list : { BrowseSongsList(), BrowseAlbumsList(), BrowsePlaylistsList(), BrowseArtistsList() })
        {
            list.Visibility(listVisibility);
        }

        EnsureAccentBrushes();
        SearchGridViewButton().Background(m_searchGridMode ? m_brushSurface : m_brushTransparent);
        SearchListViewButton().Background(m_searchGridMode ? m_brushTransparent : m_brushSurface);
        SearchGridViewButton().Foreground(m_searchGridMode ? m_brushLabelIdle : m_brushTextTertiary);
        SearchListViewButton().Foreground(m_searchGridMode ? m_brushTextTertiary : m_brushLabelIdle);
    }

    void MainWindow::RebuildBrowseResults()
    {
        m_browseSongResults.Clear();
        m_browseAlbumResults.Clear();
        m_browsePlaylistResults.Clear();
        m_browseArtistResults.Clear();

        std::vector<LastMusicPlayer::Backend::BrowseResultInput> inputs;
        std::vector<winrt::Last_Music_Player::TrackInfo> sources;
        inputs.reserve(m_searchAllResults.size() + m_searchMatchingPlaylists.size());
        sources.reserve(inputs.capacity());
        for (auto const& track : m_searchAllResults)
        {
            if (!track) { continue; }
            inputs.push_back({
                LastMusicPlayer::Backend::BrowseResultKind::Song,
                std::wstring(track.Title().c_str()),
                std::wstring(track.Artist().c_str()),
                std::wstring(track.Album().c_str()),
                {}, std::wstring(track.Duration().c_str()), HomeQueueDedupeKey(track), 0
            });
            sources.push_back(track);
        }
        for (auto const& playlist : m_searchMatchingPlaylists)
        {
            if (!playlist) { continue; }
            inputs.push_back({
                LastMusicPlayer::Backend::BrowseResultKind::Playlist,
                std::wstring(playlist.Title().c_str()),
                {}, {}, {}, {}, std::wstring(playlist.SourceUrl().c_str()),
                static_cast<std::size_t>((std::max)(0, playlist.TrackCount()))
            });
            sources.push_back(playlist);
        }

        auto const grouped = LastMusicPlayer::Backend::BuildGroupedBrowseResults(
            inputs,
            std::wstring(TrimQuery(GlobalSearchBox().Text()).c_str()),
            m_searchUrlLookup);

        auto append = [&](LastMusicPlayer::Backend::BrowseResultEntity const& entity)
        {
            if (entity.RepresentativeIndex >= sources.size()) { return; }
            auto representative = sources[entity.RepresentativeIndex];
            auto const kind = KindTag(entity.Kind);
            ResolveArtworkPresentation(representative, kind);

            winrt::Last_Music_Player::BrowseResultItem item;
            item.Kind(kind);
            item.Title(winrt::hstring(entity.Title));
            item.Subtitle(winrt::hstring(entity.Subtitle));
            item.Meta(winrt::hstring(entity.Meta));
            item.Key(winrt::hstring(entity.Key));
            item.ArtworkUrl(representative.ArtworkUrl());
            item.ArtworkSource(representative.AlbumArt());
            auto itemImplementation = winrt::get_self<BrowseResultItem>(item);
            itemImplementation->RepresentativeTrack(representative);

            std::vector<winrt::Last_Music_Player::TrackInfo> members;
            members.reserve(entity.MemberIndices.size());
            for (auto const index : entity.MemberIndices)
            {
                if (index < sources.size()) { members.push_back(sources[index]); }
            }
            itemImplementation->MemberTracks(std::move(members));

            switch (entity.Kind)
            {
            case LastMusicPlayer::Backend::BrowseResultKind::Song: m_browseSongResults.Append(item); break;
            case LastMusicPlayer::Backend::BrowseResultKind::Album: m_browseAlbumResults.Append(item); break;
            case LastMusicPlayer::Backend::BrowseResultKind::Playlist: m_browsePlaylistResults.Append(item); break;
            case LastMusicPlayer::Backend::BrowseResultKind::Artist: m_browseArtistResults.Append(item); break;
            }
        };
        for (auto const& group : grouped.Groups)
        {
            for (auto const& entity : group.Items) { append(entity); }
        }

        auto applySection = [](MUX::FrameworkElement const& section, MUXC::TextBlock const& count, std::size_t size)
        {
            section.Visibility(size == 0 ? Visibility::Collapsed : Visibility::Visible);
            count.Text(ResultCountText(size));
        };
        applySection(BrowseSongsSection(), BrowseSongsCount(), m_browseSongResults.Size());
        applySection(BrowseAlbumsSection(), BrowseAlbumsCount(), m_browseAlbumResults.Size());
        applySection(BrowsePlaylistsSection(), BrowsePlaylistsCount(), m_browsePlaylistResults.Size());
        applySection(BrowseArtistsSection(), BrowseArtistsCount(), m_browseArtistResults.Size());

        SearchResultCountText().Text(winrt::hstring(
            std::to_wstring(grouped.DisplayedEntityCount)
            + (grouped.DisplayedEntityCount == 1 ? L" item" : L" items")));
        auto const hasContent = grouped.DisplayedEntityCount > 0;
        BrowseGroupedResultsPanel().Visibility(hasContent ? Visibility::Visible : Visibility::Collapsed);
        SearchEmptyPanel().Visibility(hasContent ? Visibility::Collapsed : Visibility::Visible);
        if (!hasContent)
        {
            auto const query = std::wstring(TrimQuery(GlobalSearchBox().Text()).c_str());
            SearchEmptyTitleText().Text(winrt::hstring(L"Nothing for “" + query + L"”"));
            SearchEmptyHintText().Text(L"Check the spelling, or try a broader search.");
        }
        ApplyBrowseViewVisuals();
    }

    void MainWindow::LoadPersistedRecentSearches()
    {
        m_recentSearches = LastMusicPlayer::Backend::DecodeRecentSearches(
            std::wstring(SettingsManagerService().GetString(L"RecentSearches", L"").c_str()));
    }

    void MainWindow::RecordBrowseRecentSearch(winrt::hstring const& query)
    {
        m_recentSearches = LastMusicPlayer::Backend::RecordRecentSearch(m_recentSearches, std::wstring(query.c_str()));
        SettingsManagerService().SetString(
            L"RecentSearches",
            winrt::hstring(LastMusicPlayer::Backend::EncodeRecentSearches(m_recentSearches)));
        RebuildBrowseRecentChips();
    }

    void MainWindow::RebuildBrowseRecentChips()
    {
        auto panel = BrowseRecentSearchChips();
        if (!panel) { return; }
        panel.Children().Clear();
        for (auto const& query : m_recentSearches)
        {
            MUXC::Button button;
            button.Tag(winrt::box_value(winrt::hstring(query)));
            button.Background(m_brushSurface);
            button.BorderBrush(m_brushStroke);
            button.BorderThickness({ 1 });
            button.CornerRadius({ 14 });
            button.MinWidth(0);
            button.MinHeight(0);
            button.Padding({ 11, 6, 11, 6 });
            button.FontSize(12.5);
            button.HorizontalContentAlignment(MUX::HorizontalAlignment::Center);
            button.VerticalContentAlignment(MUX::VerticalAlignment::Center);
            button.UseSystemFocusVisuals(true);

            MUXC::StackPanel content;
            content.Orientation(MUXC::Orientation::Horizontal);
            content.Spacing(8);
            content.VerticalAlignment(MUX::VerticalAlignment::Center);
            MUXC::FontIcon history;
            history.Glyph(L"\xE823");
            history.FontSize(11);
            MUXC::TextBlock label;
            label.Text(winrt::hstring(query));
            content.Children().Append(history);
            content.Children().Append(label);
            button.Content(content);
            button.Click({ this, &MainWindow::BrowseRecentSearch_Click });
            panel.Children().Append(button);
        }
        BrowseRecentSearchesSection().Visibility(m_recentSearches.empty()
            ? Visibility::Collapsed : Visibility::Visible);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateBrowseLandingAsync(bool force)
    {
        auto lifetime = get_strong();
        if (m_browseLandingLoaded && !force) { co_return; }

        auto const epoch = ++m_browseLandingEpoch;
        auto dispatcher = DispatcherQueue();
        std::vector<winrt::Last_Music_Player::TrackInfo> genres;
        co_await winrt::resume_background();
        if (DatabaseService().IsInitialized())
        {
            genres = DatabaseService().LoadGenres();
        }
        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_browseLandingEpoch) { co_return; }

        std::unordered_map<std::wstring, winrt::Last_Music_Player::TrackInfo> byGenre;
        std::vector<std::wstring> rankedGenres;
        rankedGenres.reserve(genres.size());
        for (auto const& genre : genres)
        {
            auto const trimmed = TrimQuery(genre.Title());
            byGenre.emplace(ToLowerCopy(trimmed), genre);
            rankedGenres.emplace_back(genre.Title().c_str());
        }

        auto const appendCategory = [this](winrt::Last_Music_Player::TrackInfo const& group, winrt::hstring const& context)
        {
            auto category = group;
            category.ArtworkCaption(winrt::hstring(
                std::to_wstring(category.TrackCount())
                + (category.TrackCount() == 1 ? L" track" : L" tracks")));
            ResolveArtworkPresentation(category, context);
            m_browseCategories.Append(category);
        };

        m_browseCategories.Clear();
        for (auto const& categorySpec : LastMusicPlayer::Backend::BrowseLandingCategories(rankedGenres))
        {
            if (!categorySpec.IsCurated)
            {
                auto const folded = ToLowerCopy(winrt::hstring(categorySpec.Name));
                if (auto const found = byGenre.find(folded); found != byGenre.end())
                {
                    auto category = found->second;
                    category.Title(winrt::hstring(categorySpec.Name));
                    appendCategory(category, L"genre");
                }
                continue;
            }

            winrt::Last_Music_Player::TrackInfo category;
            category.Title(winrt::hstring(categorySpec.Name));
            category.ArtworkCaption(L"Explore");
            category.SourceKind(L"catalog-search");
            category.SourceUrl(winrt::hstring(categorySpec.Name));
            m_browseCategories.Append(category);
        }
        m_browseLandingLoaded = true;
    }
}
