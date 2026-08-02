#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/CatalogParser.h"

#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Microsoft.UI.Text.h>

#include <string>
#include <utility>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        // Bound on the decoded-artwork cache. Discovery pages are almost entirely
        // images, and a user can scroll through several storefronts in a session,
        // so the cache is capped rather than left to grow with the session.
        constexpr std::size_t kMaxCachedArtwork = 256;
    }

    winrt::fire_and_forget MainWindow::HydrateCatalogArtworkAsync(
        winrt::Microsoft::UI::Xaml::Controls::Image image,
        winrt::hstring artworkUrl)
    {
        if (!image || artworkUrl.empty())
        {
            co_return;
        }

        // GridView recycles containers as they scroll. Stamping the requested URL
        // on the target is what lets a late response tell whether the container it
        // was started for still shows the same item; without it a slow image would
        // land on whatever tile inherited the container.
        image.Tag(winrt::box_value(artworkUrl));
        image.Source(nullptr);

        auto lifetime = get_strong();
        std::wstring key{ artworkUrl.c_str() };

        auto cached = m_catalogArtworkCache.find(key);
        if (cached != m_catalogArtworkCache.end())
        {
            image.Source(cached->second);
            co_return;
        }

        auto dispatcher = DispatcherQueue();
        winrt::Windows::Storage::Streams::IBuffer buffer{ nullptr };
        try
        {
            // The artwork relay needs the account session, which BitmapImage
            // cannot attach to a URL of its own. Failures are not reported to the
            // user: a tile without artwork falls back to the generated placeholder
            // already sitting behind it, which is a better outcome than a message
            // for something purely decorative.
            buffer = co_await RemoteMusicServiceService().GetAccountArtworkAsync(artworkUrl);
        }
        catch (...)
        {
            co_return;
        }
        if (!buffer || buffer.Length() == 0)
        {
            co_return;
        }

        co_await wil::resume_foreground(dispatcher);

        winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage bitmap{ nullptr };
        try
        {
            winrt::Windows::Storage::Streams::InMemoryRandomAccessStream stream;
            co_await stream.WriteAsync(buffer);
            stream.Seek(0);
            bitmap = winrt::Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
            co_await bitmap.SetSourceAsync(stream);
        }
        catch (...)
        {
            co_return;
        }
        if (!bitmap)
        {
            co_return;
        }

        // Crude but predictable: once full, drop everything and start over rather
        // than tracking recency for what is a decorative cache.
        if (m_catalogArtworkCache.size() >= kMaxCachedArtwork)
        {
            m_catalogArtworkCache.clear();
        }
        m_catalogArtworkCache.emplace(key, bitmap);

        auto tag = image.Tag();
        if (!tag)
        {
            co_return;
        }
        auto currentUrl = winrt::unbox_value_or<winrt::hstring>(tag, winrt::hstring{});
        if (currentUrl != artworkUrl)
        {
            // The container was recycled onto a different item while this was in
            // flight. The cache entry above still stands, so the tile that wants
            // this image will get it without another request.
            co_return;
        }
        image.Source(bitmap);
    }

    void MainWindow::ClearCatalogArtworkCache()
    {
        m_catalogArtworkCache.clear();
    }

    void MainWindow::CatalogTileArt_Loaded(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto image = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Image>();
        if (!image)
        {
            return;
        }
        // Inside a DataTemplate the element's DataContext is the item, which is
        // also what changes under it when the container is recycled.
        auto track = image.DataContext().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }
        HydrateCatalogArtworkAsync(image, track.ArtworkUrl());
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::CatalogItemToTrack(
        LastMusicPlayer::Backend::CatalogItem const& item,
        int32_t index)
    {
        winrt::Last_Music_Player::TrackInfo track;
        track.Title(item.Title);
        track.Artist(item.Subtitle);
        track.Album(item.AlbumName);
        track.Index(index);
        track.ArtworkUrl(item.ArtworkUrl);
        // SourceUrl is the whole point: playing a catalog song posts this URL to
        // the existing resolve path, which mirrors it onto something playable.
        track.SourceUrl(item.SourceUrl);
        track.Provider(item.Provider);
        track.SourceKind(L"remote");
        track.SourceLabel(L"Catalog");
        track.DateAdded(L"Catalog");
        track.RemoteCatalogId(item.CatalogId);
        track.AlbumCatalogId(item.AlbumCatalogId);
        track.ArtistCatalogId(item.ArtistCatalogId);
        track.TrackCount(item.TrackCount);
        // The resource type rides along so a tile click knows whether to play a
        // song or open a detail page.
        track.Genre(LastMusicPlayer::Backend::CatalogResourceTypeName(item.Type));

        auto seconds = item.DurationMs > 0 ? static_cast<double>(item.DurationMs) / 1000.0 : 0.0;
        track.DurationSeconds(seconds);
        track.Duration(seconds > 0.0
            ? winrt::hstring{ LastMusicPlayer::Frontend::UIHelpers::FormatTime(seconds) }
            : winrt::hstring{});
        return track;
    }

    void MainWindow::ShowDiscoverShelvesPage()
    {
        ++m_discoverNavigationEpoch;
        using winrt::Microsoft::UI::Xaml::Visibility;
        DiscoverShelvesPage().Visibility(Visibility::Visible);
        DiscoverChartPage().Visibility(Visibility::Collapsed);
        DiscoverDetailPage().Visibility(Visibility::Collapsed);
    }

    void MainWindow::SetDiscoverStatus(winrt::hstring const& message, bool canRetry)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        DiscoverStatusText().Text(message);
        DiscoverStatusText().Visibility(message.empty() ? Visibility::Collapsed : Visibility::Visible);
        DiscoverRetryButton().Visibility(canRetry ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::UpdateDiscoverAvailability()
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        using LastMusicPlayer::Backend::AccountSessionStatus;
        using LastMusicPlayer::Backend::RemoteAccessMode;

        if (!BrowseButton())
        {
            return;
        }
        auto status = AccountSessionService().Status();
        bool available = RemoteMusicServiceService().Mode() == RemoteAccessMode::Account
            && (status == AccountSessionStatus::Validated || status == AccountSessionStatus::Offline);
        BrowseButton().Visibility(available ? Visibility::Visible : Visibility::Collapsed);

        if (available)
        {
            return;
        }

        // Leaving Account mode invalidates everything on the page, including the
        // artwork cache, whose entries were fetched with the old session.
        ++m_discoverEpoch;
        m_discoverLoaded = false;
        DiscoverShelvesPanel().Children().Clear();
        m_discoverChartItems.Clear();
        m_discoverDetailTracks.Clear();
        ClearCatalogArtworkCache();
        if (BrowseViewContainer().Visibility() == Visibility::Visible)
        {
            SongsButton_Click(nullptr, nullptr);
        }
    }

    winrt::hstring MainWindow::CurrentDiscoverStorefront()
    {
        if (m_discoverStorefront.empty())
        {
            m_discoverStorefront = SettingsManagerService().GetString(
                L"CatalogStorefront",
                LastMusicPlayer::Backend::DefaultCatalogStorefront);
        }
        if (m_discoverStorefront.empty())
        {
            m_discoverStorefront = LastMusicPlayer::Backend::DefaultCatalogStorefront;
        }
        return m_discoverStorefront;
    }

    void MainWindow::PopulateDiscoverStorefronts(
        std::vector<LastMusicPlayer::Backend::CatalogStorefront> const& storefronts)
    {
        auto selector = DiscoverStorefrontSelector();
        if (!selector || storefronts.empty())
        {
            return;
        }

        auto current = CurrentDiscoverStorefront();
        m_suppressDiscoverStorefrontChange = true;
        selector.Items().Clear();
        int selectedIndex = -1;
        for (uint32_t index = 0; index < storefronts.size(); ++index)
        {
            auto const& storefront = storefronts[index];
            winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem item;
            item.Content(winrt::box_value(storefront.Name));
            item.Tag(winrt::box_value(storefront.Code));
            selector.Items().Append(item);
            if (storefront.Code == current)
            {
                selectedIndex = static_cast<int>(index);
            }
        }
        selector.SelectedIndex(selectedIndex);
        m_suppressDiscoverStorefrontChange = false;
    }

    void MainWindow::BuildDiscoverShelves(LastMusicPlayer::Backend::CatalogDiscovery const& discovery)
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        auto panel = DiscoverShelvesPanel();
        panel.Children().Clear();

        auto tileTemplate = BrowseViewContainer().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"CatalogTileTemplate" }))
            .try_as<DataTemplate>();
        auto containerStyle = Application::Current().Resources()
            .Lookup(winrt::box_value(winrt::hstring{ L"CardTileContainerStyle" }))
            .try_as<Style>();

        for (auto const& shelf : discovery.Shelves)
        {
            StackPanel section;
            section.Spacing(0);
            section.Margin(ThicknessHelper::FromLengths(0, 0, 0, 24));

            Grid header;
            header.Margin(ThicknessHelper::FromLengths(0, 0, 0, 16));
            ColumnDefinition titleColumn;
            titleColumn.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
            ColumnDefinition actionColumn;
            actionColumn.Width(GridLengthHelper::Auto());
            header.ColumnDefinitions().Append(titleColumn);
            header.ColumnDefinitions().Append(actionColumn);

            TextBlock title;
            title.Text(shelf.Title);
            title.Style(Application::Current().Resources()
                .Lookup(winrt::box_value(winrt::hstring{ L"TitleH3" }))
                .try_as<Style>());
            title.VerticalAlignment(VerticalAlignment::Center);
            header.Children().Append(title);

            // "See all" only exists when the service told us a chart backs this
            // shelf. A shelf with no chart is a dead end, so no affordance.
            if (!shelf.ChartId.empty())
            {
                Button seeAll;
                seeAll.Content(winrt::box_value(winrt::hstring{ L"See all \x203A" }));
                seeAll.Background(m_brushTransparent);
                seeAll.BorderThickness(ThicknessHelper::FromUniformLength(0));
                seeAll.Padding(ThicknessHelper::FromLengths(6, 2, 6, 2));
                seeAll.FontSize(12);
                seeAll.FontWeight(winrt::Microsoft::UI::Text::FontWeights::SemiBold());
                seeAll.Foreground(m_brushAccent);
                seeAll.HorizontalAlignment(HorizontalAlignment::Right);
                seeAll.VerticalAlignment(VerticalAlignment::Center);
                Grid::SetColumn(seeAll, 1);

                auto chartType = LastMusicPlayer::Backend::CatalogResourceTypeName(shelf.ItemType);
                auto shelfTitle = shelf.Title;
                seeAll.Click([this, chartType, shelfTitle](auto&&, auto&&)
                {
                    RunDetached(OpenDiscoverChartAsync(chartType, shelfTitle));
                });
                header.Children().Append(seeAll);
            }
            section.Children().Append(header);

            auto items = winrt::single_threaded_observable_vector<winrt::Last_Music_Player::TrackInfo>();
            int32_t index = 1;
            for (auto const& item : shelf.Items)
            {
                items.Append(CatalogItemToTrack(item, index++));
            }

            GridView grid;
            grid.Height(260);
            grid.IsItemClickEnabled(true);
            grid.SelectionMode(ListViewSelectionMode::None);
            if (containerStyle) grid.ItemContainerStyle(containerStyle);
            if (tileTemplate) grid.ItemTemplate(tileTemplate);
            ScrollViewer::SetHorizontalScrollBarVisibility(grid, ScrollBarVisibility::Hidden);
            ScrollViewer::SetHorizontalScrollMode(grid, ScrollMode::Enabled);
            ScrollViewer::SetVerticalScrollMode(grid, ScrollMode::Disabled);
            ScrollViewer::SetVerticalScrollBarVisibility(grid, ScrollBarVisibility::Hidden);

            ItemsPanelTemplate panelTemplate = winrt::Microsoft::UI::Xaml::Markup::XamlReader::Load(
                LR"(<ItemsPanelTemplate xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">
                        <ItemsStackPanel Orientation="Horizontal" />
                    </ItemsPanelTemplate>)").try_as<ItemsPanelTemplate>();
            if (panelTemplate) grid.ItemsPanel(panelTemplate);

            grid.ItemsSource(items);
            grid.ItemClick({ this, &MainWindow::DiscoverItem_ItemClick });
            section.Children().Append(grid);

            panel.Children().Append(section);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateDiscoverAsync(bool force)
    {
        auto lifetime = get_strong();
        if (m_discoverLoaded && !force)
        {
            co_return;
        }

        using LastMusicPlayer::Backend::RemoteAccessMode;
        if (RemoteMusicServiceService().Mode() != RemoteAccessMode::Account)
        {
            SetDiscoverStatus(L"Sign in to your account to browse the catalog.", false);
            co_return;
        }

        auto epoch = ++m_discoverEpoch;
        DiscoverShelvesPanel().Children().Clear();
        SetDiscoverStatus(L"Loading...", false);

        auto storefront = CurrentDiscoverStorefront();
        winrt::hstring discoveryPayload;
        winrt::hstring storefrontsPayload;
        try
        {
            discoveryPayload = co_await RemoteMusicServiceService().GetCatalogDiscoveryAsync(storefront);
        }
        catch (winrt::hresult_error const& error)
        {
            if (epoch != m_discoverEpoch) co_return;
            // Distinct from "no data": the request itself did not complete, so
            // offer a retry rather than implying the region is empty.
            SetDiscoverStatus(
                LastMusicPlayer::Backend::IsAccountUnauthorized(error)
                    ? winrt::hstring{ L"Your account session has expired. Sign in again to browse." }
                    : winrt::hstring{ L"Could not reach the catalog service." },
                !LastMusicPlayer::Backend::IsAccountUnauthorized(error));
            co_return;
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch) co_return;
            SetDiscoverStatus(L"Could not reach the catalog service.", true);
            co_return;
        }
        if (epoch != m_discoverEpoch)
        {
            co_return;
        }

        auto discovery = LastMusicPlayer::Backend::ParseCatalogDiscovery(discoveryPayload);
        if (discovery.Shelves.empty())
        {
            // The request succeeded and produced nothing usable. Either this
            // storefront has no charts, or the payload shape moved; both look the
            // same from here, so the message says what is observable.
            SetDiscoverStatus(L"Nothing to show for this region right now.", true);
        }
        else
        {
            BuildDiscoverShelves(discovery);
            SetDiscoverStatus(L"", false);
            m_discoverLoaded = true;
        }

        // The storefront list is cosmetic, so a failure here leaves the existing
        // picker contents alone rather than disturbing a page that already loaded.
        try
        {
            storefrontsPayload = co_await RemoteMusicServiceService().GetCatalogStorefrontsAsync();
        }
        catch (...)
        {
            co_return;
        }
        if (epoch != m_discoverEpoch)
        {
            co_return;
        }
        PopulateDiscoverStorefronts(
            LastMusicPlayer::Backend::ParseCatalogStorefronts(storefrontsPayload));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDiscoverChartAsync(
        winrt::hstring chartType,
        winrt::hstring title)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;
        ++m_discoverNavigationEpoch;

        // The service names chart types in the plural; shelves carry the
        // singular resource type.
        m_discoverChartType = chartType == L"album" ? winrt::hstring{ L"albums" }
            : chartType == L"playlist" ? winrt::hstring{ L"playlists" }
            : winrt::hstring{ L"songs" };
        m_discoverChartNextOffset = 0;
        m_discoverChartHasMore = false;
        m_discoverChartItems.Clear();
        DiscoverChartGridView().ItemsSource(m_discoverChartItems);
        DiscoverChartTitleText().Text(title);
        DiscoverChartKindText().Text(L"Chart");
        DiscoverChartMoreButton().Visibility(Visibility::Collapsed);

        DiscoverShelvesPage().Visibility(Visibility::Collapsed);
        DiscoverDetailPage().Visibility(Visibility::Collapsed);
        DiscoverChartPage().Visibility(Visibility::Visible);

        co_await LoadDiscoverChartPageAsync(0);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadDiscoverChartPageAsync(int32_t offset)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto epoch = m_discoverEpoch;
        auto navigationEpoch = m_discoverNavigationEpoch;
        auto chartType = m_discoverChartType;
        auto storefront = CurrentDiscoverStorefront();
        DiscoverChartMoreButton().IsEnabled(false);
        winrt::hstring payload;
        try
        {
            payload = co_await RemoteMusicServiceService().GetCatalogChartAsync(
                storefront,
                chartType,
                50,
                offset);
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch
                || navigationEpoch != m_discoverNavigationEpoch)
            {
                co_return;
            }
            DiscoverChartMoreButton().IsEnabled(true);
            ShowPlaybackNotice(L"Could not load more of this chart.");
            co_return;
        }
        if (epoch != m_discoverEpoch
            || navigationEpoch != m_discoverNavigationEpoch
            || chartType != m_discoverChartType
            || storefront != CurrentDiscoverStorefront())
        {
            co_return;
        }

        auto page = LastMusicPlayer::Backend::ParseCatalogChartPage(payload);
        auto index = static_cast<int32_t>(m_discoverChartItems.Size()) + 1;
        for (auto const& item : page.Items)
        {
            m_discoverChartItems.Append(CatalogItemToTrack(item, index++));
        }
        if (!page.Title.empty())
        {
            DiscoverChartTitleText().Text(page.Title);
        }

        m_discoverChartHasMore = page.HasNextOffset && !page.Items.empty();
        m_discoverChartNextOffset = page.NextOffset;
        DiscoverChartMoreButton().IsEnabled(true);
        DiscoverChartMoreButton().Visibility(m_discoverChartHasMore ? Visibility::Visible : Visibility::Collapsed);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDiscoverResourceAsync(
        winrt::hstring kind,
        winrt::hstring catalogId,
        winrt::hstring title)
    {
        auto lifetime = get_strong();
        using winrt::Microsoft::UI::Xaml::Visibility;

        if (catalogId.empty())
        {
            ShowPlaybackNotice(L"That item has no catalog page.");
            co_return;
        }

        auto navigationEpoch = ++m_discoverNavigationEpoch;
        auto epoch = m_discoverEpoch;
        auto storefront = CurrentDiscoverStorefront();
        m_discoverDetailTracks.Clear();
        DiscoverDetailTracksListView().ItemsSource(m_discoverDetailTracks);
        DiscoverDetailTitleText().Text(title);
        DiscoverDetailKindText().Text(kind == L"playlists" ? L"Playlist" : L"Album");
        DiscoverDetailSubtitleText().Text(L"Loading...");
        DiscoverDetailDescriptionText().Text(L"");
        DiscoverDetailArt().Tag(nullptr);
        DiscoverDetailArt().Source(nullptr);

        DiscoverShelvesPage().Visibility(Visibility::Collapsed);
        DiscoverChartPage().Visibility(Visibility::Collapsed);
        DiscoverDetailPage().Visibility(Visibility::Visible);

        winrt::hstring payload;
        try
        {
            payload = co_await RemoteMusicServiceService().GetCatalogResourceAsync(
                kind,
                catalogId,
                storefront);
        }
        catch (...)
        {
            if (epoch != m_discoverEpoch
                || navigationEpoch != m_discoverNavigationEpoch)
            {
                co_return;
            }
            DiscoverDetailSubtitleText().Text(L"Could not load this item.");
            co_return;
        }
        if (epoch != m_discoverEpoch
            || navigationEpoch != m_discoverNavigationEpoch
            || storefront != CurrentDiscoverStorefront())
        {
            co_return;
        }

        auto detail = LastMusicPlayer::Backend::ParseCatalogResourceDetail(payload, kind);
        if (!detail.Resource.Title.empty())
        {
            DiscoverDetailTitleText().Text(detail.Resource.Title);
        }
        DiscoverDetailDescriptionText().Text(detail.Resource.Description);

        int32_t index = 1;
        for (auto const& item : detail.Tracks)
        {
            m_discoverDetailTracks.Append(CatalogItemToTrack(item, index++));
        }

        std::wstring subtitle;
        if (!detail.Resource.Subtitle.empty())
        {
            subtitle = std::wstring{ detail.Resource.Subtitle.c_str() } + L" \x00B7 ";
        }
        subtitle += std::to_wstring(m_discoverDetailTracks.Size());
        subtitle += m_discoverDetailTracks.Size() == 1 ? L" song" : L" songs";
        DiscoverDetailSubtitleText().Text(winrt::hstring{ subtitle });

        if (!detail.Resource.ArtworkUrl.empty())
        {
            HydrateCatalogArtworkAsync(DiscoverDetailArt(), detail.Resource.ArtworkUrl);
        }
    }

    void MainWindow::DiscoverBack_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ShowDiscoverShelvesPage();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        co_await HydrateDiscoverAsync(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverStorefront_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)args;
        auto lifetime = get_strong();
        if (m_suppressDiscoverStorefrontChange)
        {
            co_return;
        }
        auto selector = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBox>();
        if (!selector)
        {
            co_return;
        }
        auto item = selector.SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem>();
        if (!item)
        {
            co_return;
        }
        auto code = winrt::unbox_value_or<winrt::hstring>(item.Tag(), winrt::hstring{});
        if (code.empty() || code == m_discoverStorefront)
        {
            co_return;
        }

        m_discoverStorefront = code;
        SettingsManagerService().SetString(L"CatalogStorefront", code);
        // Artwork is per-storefront content; keeping the old entries would only
        // hold memory for images no longer on screen.
        ClearCatalogArtworkCache();
        ShowDiscoverShelvesPage();
        co_await HydrateDiscoverAsync(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverItem_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto lifetime = get_strong();
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            co_return;
        }

        auto type = track.Genre();
        if (type == L"album")
        {
            co_await OpenDiscoverResourceAsync(L"albums", track.RemoteCatalogId(), track.Title());
            co_return;
        }
        if (type == L"playlist")
        {
            co_await OpenDiscoverResourceAsync(L"playlists", track.RemoteCatalogId(), track.Title());
            co_return;
        }
        // A song tile plays through the ordinary remote path: PlayTrack sees an
        // http source URL with no stream URL and routes it to the resolver.
        PlayTrack(track);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::DiscoverChartMore_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        if (!m_discoverChartHasMore)
        {
            co_return;
        }
        co_await LoadDiscoverChartPageAsync(m_discoverChartNextOffset);
    }

    void MainWindow::DiscoverDetailTrack_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }
        // Queue the whole detail list so the rest of the album or playlist plays
        // on, rather than stopping after the one track that was clicked.
        QueueAndPlayObservable(m_discoverDetailTracks, track);
    }

    void MainWindow::DiscoverDetailPlay_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_discoverDetailTracks.Size() == 0)
        {
            ShowPlaybackNotice(L"There is nothing to play here.");
            return;
        }
        QueueAndPlayObservable(m_discoverDetailTracks, m_discoverDetailTracks.GetAt(0));
    }

    void MainWindow::DiscoverDetailQueue_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_discoverDetailTracks.Size() == 0)
        {
            ShowPlaybackNotice(L"There is nothing to queue here.");
            return;
        }
        for (auto const& track : m_discoverDetailTracks)
        {
            AddSongsTrackToQueue(track);
        }
        ShowPlaybackNotice(m_discoverDetailTracks.Size() == 1
            ? winrt::hstring{ L"Added 1 song to the queue." }
            : winrt::hstring{ L"Added " + std::to_wstring(m_discoverDetailTracks.Size()) + L" songs to the queue." });
    }
}
