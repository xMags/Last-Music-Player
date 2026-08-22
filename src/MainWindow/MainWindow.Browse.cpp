#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/RecentSearchStore.h"
#include "Backend/ResumePositionPolicy.h"
#include "Backend/SearchFacetPolicy.h"
#include "Backend/TrackSearchPolicy.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        namespace MUX = winrt::Microsoft::UI::Xaml;
        namespace MUXC = winrt::Microsoft::UI::Xaml::Controls;

        using MUX::Visibility;

        constexpr std::size_t kBrowseSongPreviewLimit = 5;
        constexpr std::size_t kBrowseFacetLimit = 18;
        constexpr std::size_t kBrowseResumeLimit = 3;

        winrt::hstring TagText(winrt::Windows::Foundation::IInspectable const& value)
        {
            try
            {
                return winrt::unbox_value_or<winrt::hstring>(value, L"");
            }
            catch (...)
            {
                return {};
            }
        }

        winrt::Last_Music_Player::TrackInfo FirstMatchingFacetSource(
            std::vector<winrt::Last_Music_Player::TrackInfo> const& tracks,
            LastMusicPlayer::Backend::SearchFacet const& facet)
        {
            auto const foldedTitle = ToLowerCopy(winrt::hstring(facet.Title));
            auto const foldedSubtitle = ToLowerCopy(winrt::hstring(facet.Subtitle));
            auto const found = std::find_if(tracks.begin(), tracks.end(), [&](auto const& track)
            {
                if (!track)
                {
                    return false;
                }
                if (facet.Kind == LastMusicPlayer::Backend::SearchFacetKind::Album)
                {
                    return ToLowerCopy(track.Album()) == foldedTitle
                        && (foldedSubtitle.empty() || ToLowerCopy(track.Artist()) == foldedSubtitle);
                }
                return ToLowerCopy(track.Artist()) == foldedTitle;
            });
            return found == tracks.end() ? nullptr : *found;
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
        else if (!m_searchAllResults.empty())
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

        // These tiles are library groups, so a click opens that group's detail
        // page. Typing the name into the search box instead would run the
        // title/artist/album LIKE query, which has no Genre clause: every genre
        // tile whose name was not also a song or album title landed on the
        // empty state while still advertising a track count.
        //
        // The tiles are built from the library, so this navigates the library
        // regardless of the active scope; a catalog text search for a genre
        // word would return something unrelated to what the tile counted.
        auto const artistTile = category.SourceKind() == L"artist";
        auto const kind = artistTile ? winrt::hstring{ L"artist" } : winrt::hstring{ L"genre" };
        auto const tab = artistTile ? LibTabArtists() : LibTabGenres();

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

    void MainWindow::BrowseResume_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto track = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!track)
        {
            return;
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> queue{ track };
        SetPlaybackQueue(queue, 0);
        PlayTrack(track);

        // Set after PlayTrack, not before: PlayTrack clears any pending seek as
        // its last step, so an earlier assignment would be thrown away. The seek
        // itself is applied by OnMediaOpened, which runs after this returns.
        if (LastMusicPlayer::Backend::CanResumeFrom(
            track.ResumePositionSeconds(),
            track.DurationSeconds()))
        {
            m_pendingResumeSeekSeconds = track.ResumePositionSeconds();
        }
    }

    void MainWindow::BrowseClearQuery_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        GlobalSearchBox().Text(L"");
        GlobalSearchBox().Focus(MUX::FocusState::Programmatic);
    }

    void MainWindow::BrowseScope_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const scope = element ? std::wstring(TagText(element.Tag()).c_str()) : std::wstring{};
        if ((scope != L"Library" && scope != L"Catalog") || scope == m_searchScope)
        {
            return;
        }

        m_searchScope = scope;
        ApplyBrowseScopeVisuals();
        auto const query = TrimQuery(GlobalSearchBox().Text());
        if (query.empty())
        {
            ShowBrowseLanding();
            return;
        }

        if (scope == L"Library")
        {
            // Cancel a catalog request before it can repopulate a page the
            // listener just narrowed to their library.
            ++m_searchDebounceId;
            ++m_searchRequestId;
            RebuildBrowseResults();
            return;
        }
        RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::BrowseFilter_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto element = sender.try_as<MUX::FrameworkElement>();
        auto const filter = element ? std::wstring(TagText(element.Tag()).c_str()) : std::wstring{};
        if (filter != L"All" && filter != L"Songs" && filter != L"Albums"
            && filter != L"Artists" && filter != L"Playlists")
        {
            return;
        }
        m_searchFilter = filter;
        ApplyBrowseFilterVisuals();
        RebuildBrowseResults();
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
        m_searchFilter = L"All";
        ApplyBrowseFilterVisuals();
        GlobalSearchBox().Text(query);
        RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::BrowseTopResult_Tapped(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::Input::TappedRoutedEventArgs const& args)
    {
        (void)sender;
        if (!m_searchTopResult)
        {
            return;
        }
        PlayTopSearchResult();
        args.Handled(true);
    }

    void MainWindow::BrowseTopResultPlay_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_searchTopResult)
        {
            return;
        }
        PlayTopSearchResult();
    }

    void MainWindow::PlayTopSearchResult()
    {
        // For a song this is the one track; for an album it is every track of
        // that album the page is holding, starting at the first.
        auto queue = m_searchTopResultQueue.empty()
            ? std::vector<winrt::Last_Music_Player::TrackInfo>{ m_searchTopResult }
            : m_searchTopResultQueue;
        SetPlaybackQueue(queue, 0);
        PlayTrack(queue.front());
    }

    void MainWindow::BrowseTopResultDownload_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto const queued = EnqueueAutomaticDownload(m_searchTopResult, L"Browse result");
        ShowPlaybackNotice(queued
            ? winrt::hstring{ L"Queued for offline download" }
            : winrt::hstring{ L"This track is already on this PC or already queued" });
        RefreshDownloadsView(true);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::BrowseTopResultAdd_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_searchTopResult)
        {
            co_await AddTrackToPlaylistAsync(m_searchTopResult);
        }
    }

    void MainWindow::BrowseFacet_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto facet = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!facet)
        {
            return;
        }

        // The facet grid only ever holds playlists while the Playlists filter
        // is the one that built it, so the filter is the discriminator. The
        // cards themselves are the untouched playlist objects.
        if (m_searchFilter == L"Playlists")
        {
            ShowPrimaryView(L"Library");
            if (LibTabPlaylists())
            {
                LibTabPlaylists().IsChecked(true);
                LibraryTab_Checked(LibTabPlaylists(), nullptr);
            }
            ShowLibraryDetail(
                L"playlist",
                facet.SourceUrl(),
                facet.Title(),
                facet.Artist(),
                ApprovedDetailArtwork(facet, L"playlist"),
                facet);
            return;
        }

        // The remote provider exposes track search, not album/artist detail.
        // Drilling into a derived card therefore tightens the same search to
        // its title and shows songs, which works for both local and catalog hits
        // instead of opening an empty local-only detail page.
        m_searchFilter = L"Songs";
        ApplyBrowseFilterVisuals();
        GlobalSearchBox().Text(facet.Title());
        RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::BrowseFacet_ContainerContentChanging(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUXC::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        QueueContainerArtwork(args);
        if (args.InRecycleQueue())
        {
            return;
        }

        auto facet = args.Item().try_as<winrt::Last_Music_Player::TrackInfo>();
        auto root = args.ItemContainer()
            ? args.ItemContainer().ContentTemplateRoot().try_as<MUXC::StackPanel>()
            : nullptr;
        if (!facet || !root || root.Children().Size() == 0)
        {
            return;
        }
        if (auto art = root.Children().GetAt(0).try_as<MUXC::Border>())
        {
            art.CornerRadius(facet.SourceKind() == L"search-artist"
                ? MUX::CornerRadius{ 80 }
                : MUX::CornerRadius{ 12 });
        }
    }

    // The old Browse surface offered list/grid and sort controls. Keeping these
    // handlers makes any stale automation command harmless, while the redesign
    // renders the intended top-result/list/facet composition instead.
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

    void MainWindow::SearchSort_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        MUX::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<MUXC::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }
        auto const sort = LastMusicPlayer::Backend::ParseSearchResultSort(
            ReadTagString(item.Tag()).c_str());
        auto const tag = LastMusicPlayer::Backend::SearchResultSortTag(sort);
        m_searchSort.assign(tag.begin(), tag.end());
        if (SearchSortLabel())
        {
            SearchSortLabel().Text(winrt::hstring(
                LastMusicPlayer::Backend::SearchResultSortLabel(sort)));
        }
        ApplySearchResultSort();
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
        ApplyBrowseScopeVisuals();
        RebuildBrowseRecentChips();
        BrowseResumeSection().Visibility(m_browseResumeTracks.Size() == 0
            ? Visibility::Collapsed
            : Visibility::Visible);
    }

    void MainWindow::ShowBrowseSearchLoading()
    {
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Visible);
        SearchErrorPanel().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        BrowseTopAndSongs().Visibility(Visibility::Collapsed);
        BrowseFacetSection().Visibility(Visibility::Collapsed);
        ApplyBrowseScopeVisuals();
        ApplyBrowseFilterVisuals();
    }

    void MainWindow::ShowBrowseSearchResults(winrt::hstring const& query)
    {
        (void)query;
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Collapsed);
        RebuildBrowseResults();
    }

    void MainWindow::ShowBrowseSearchError(winrt::hstring const& message)
    {
        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        BrowseClearQueryButton().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        BrowseTopAndSongs().Visibility(Visibility::Collapsed);
        BrowseFacetSection().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Visible);
        SearchErrorMessageText().Text(message);
    }

    void MainWindow::SetSearchGridMode(bool gridMode)
    {
        m_searchGridMode = gridMode;
    }

    std::vector<winrt::Last_Music_Player::TrackInfo> MainWindow::BrowseScopedResults() const
    {
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        tracks.reserve(m_searchAllResults.size());
        for (auto const& track : m_searchAllResults)
        {
            if (!track || (m_searchScope == L"Library" && !track.IsInLibrary()))
            {
                continue;
            }
            tracks.push_back(track);
        }
        return tracks;
    }

    void MainWindow::ApplySearchResultSort()
    {
        auto const sort = LastMusicPlayer::Backend::ParseSearchResultSort(m_searchSort);
        std::vector<LastMusicPlayer::Backend::SearchSortKey> keys;
        keys.reserve(m_searchAllResults.size());
        for (auto const& track : m_searchAllResults)
        {
            keys.push_back({
                std::wstring(track.Title().c_str()),
                std::wstring(track.Artist().c_str()),
                track.DurationSeconds()
            });
        }
        auto const order = LastMusicPlayer::Backend::SearchResultOrder(keys, sort);
        std::vector<winrt::Last_Music_Player::TrackInfo> reordered;
        reordered.reserve(order.size());
        for (auto const index : order)
        {
            reordered.push_back(m_searchAllResults[index]);
        }
        m_searchAllResults = std::move(reordered);
        RebuildBrowseResults();
    }

    void MainWindow::RebuildBrowseResults()
    {
        ApplyBrowseScopeVisuals();
        ApplyBrowseFilterVisuals();

        auto scoped = BrowseScopedResults();
        auto const wantsSongs = m_searchFilter == L"All" || m_searchFilter == L"Songs";
        auto const wantsAlbums = m_searchFilter == L"All" || m_searchFilter == L"Albums";
        auto const wantsArtists = m_searchFilter == L"Artists";
        auto const wantsPlaylists = m_searchFilter == L"Playlists";

        m_searchTracks.Clear();
        m_searchFacets.Clear();
        m_searchTopResult = nullptr;
        m_searchTopResultQueue.clear();
        auto topIsAlbum = false;
        winrt::hstring topTitle;
        if (!scoped.empty())
        {
            std::vector<LastMusicPlayer::Backend::SearchFacetInput> candidates;
            candidates.reserve(scoped.size());
            for (auto const& track : scoped)
            {
                candidates.push_back({
                    std::wstring(track.Title().c_str()),
                    {},
                    std::wstring(track.Artist().c_str()),
                    std::wstring(track.Album().c_str()),
                    {},
                    {},
                    track.IsInLibrary(),
                    0
                });
            }

            LastMusicPlayer::Backend::TopSearchResult top;
            if (LastMusicPlayer::Backend::ChooseTopSearchResult(
                candidates,
                std::wstring(TrimQuery(GlobalSearchBox().Text()).c_str()),
                top))
            {
                m_searchTopResult = scoped[top.Index];
                topIsAlbum = top.Kind == LastMusicPlayer::Backend::TopResultKind::Album;
                topTitle = winrt::hstring(top.Title);
                if (topIsAlbum)
                {
                    // The card names an album, so its play action has to mean
                    // the album. Anything less would play one track under a
                    // heading that promised the record.
                    auto const foldedAlbum = ToLowerCopy(m_searchTopResult.Album());
                    for (auto const& track : scoped)
                    {
                        if (ToLowerCopy(track.Album()) == foldedAlbum)
                        {
                            m_searchTopResultQueue.push_back(track);
                        }
                    }
                }
                if (m_searchTopResultQueue.empty())
                {
                    m_searchTopResultQueue.push_back(m_searchTopResult);
                }
            }
        }
        if (wantsSongs)
        {
            auto const take = (std::min)(kBrowseSongPreviewLimit, scoped.size());
            for (std::size_t index = 0; index < take; ++index)
            {
                auto track = scoped[index];
                track.Index(static_cast<int32_t>(index + 1));
                ResolveArtworkPresentation(track, L"track");
                m_searchTracks.Append(track);
            }
        }

        if (m_searchTopResult && wantsSongs)
        {
            BrowseTopResultTitle().Text(topTitle);
            BrowseTopResultKindText().Text(topIsAlbum ? L"ALBUM" : L"SONG");
            BrowseTopResultSubtitle().Text(m_searchTopResult.Artist());
            BrowseTopResultArt().DataContext(m_searchTopResult);
            BrowseTopResultArt().Source(m_searchTopResult.AlbumArt());
            QueueAccountArtworkImage(
                BrowseTopResultArt(),
                m_searchTopResult.ArtworkUrl(),
                ArtworkDetail::Tile,
                m_searchTopResult);
        }
        BrowseTopAndSongs().Visibility(m_searchTopResult && wantsSongs
            ? Visibility::Visible
            : Visibility::Collapsed);

        if (wantsAlbums || wantsArtists)
        {
            auto const kind = wantsArtists
                ? LastMusicPlayer::Backend::SearchFacetKind::Artist
                : LastMusicPlayer::Backend::SearchFacetKind::Album;
            std::vector<LastMusicPlayer::Backend::SearchFacetInput> inputs;
            inputs.reserve(scoped.size());
            for (auto const& track : scoped)
            {
                inputs.push_back({
                    std::wstring(track.Title().c_str()),
                    {},
                    std::wstring(track.Artist().c_str()),
                    std::wstring(track.Album().c_str()),
                    std::wstring(track.Genre().c_str()),
                    kind == LastMusicPlayer::Backend::SearchFacetKind::Album
                        ? ToLowerCopy(track.Album()) + L"|" + ToLowerCopy(track.Artist())
                        : ToLowerCopy(track.Artist()),
                    track.IsInLibrary(),
                    0
                });
            }

            auto const facets = LastMusicPlayer::Backend::BuildSearchFacets(
                inputs, kind, kBrowseFacetLimit);
            for (auto const& facet : facets)
            {
                auto const source = FirstMatchingFacetSource(scoped, facet);
                if (!source)
                {
                    continue;
                }
                // TrackInfo is a runtime class, so assigning one shares the
                // object rather than copying it. Retitling a result in place
                // would rewrite the very row the songs list above is showing
                // and replace its playable SourceUrl. Each card is therefore
                // its own object carrying only what the tile template, the
                // artwork fetch and the drill-down read.
                winrt::Last_Music_Player::TrackInfo card;
                card.Title(winrt::hstring(facet.Title));
                card.Artist(kind == LastMusicPlayer::Backend::SearchFacetKind::Artist
                    ? winrt::hstring(std::to_wstring(facet.Count) + (facet.Count == 1 ? L" song" : L" songs"))
                    : winrt::hstring(facet.Subtitle));
                card.Album(source.Album());
                card.SourceKind(kind == LastMusicPlayer::Backend::SearchFacetKind::Artist
                    ? L"search-artist"
                    : L"search-album");
                // Carried from the matched track so the artwork pipeline can
                // still resolve a cover: QueueAccountArtworkImage falls back to
                // SourceUrl when a card has no direct artwork URL.
                card.SourceUrl(source.SourceUrl());
                card.ArtworkUrl(source.ArtworkUrl());
                card.AlbumArt(source.AlbumArt());
                card.Provider(source.Provider());
                card.TrackCount(static_cast<int32_t>(facet.Count));
                card.IsInLibrary(facet.InLibrary);
                ResolveArtworkPresentation(card, kind == LastMusicPlayer::Backend::SearchFacetKind::Artist
                    ? L"artist"
                    : L"album");
                m_searchFacets.Append(card);
            }
            BrowseFacetHeading().Text(wantsArtists ? L"ARTISTS" : L"ALBUMS");
        }
        else if (wantsPlaylists)
        {
            // The playlists go in as themselves. Stamping a marker SourceKind
            // on them would mutate the shared playlist objects the library
            // surfaces bind to; the click handler reads m_searchFilter instead.
            for (auto const& playlist : m_searchMatchingPlaylists)
            {
                ResolveArtworkPresentation(playlist, L"playlist");
                m_searchFacets.Append(playlist);
            }
            BrowseFacetHeading().Text(L"PLAYLISTS");
        }

        BrowseFacetSection().Visibility(m_searchFacets.Size() == 0
            ? Visibility::Collapsed
            : Visibility::Visible);

        auto const hasContent = (wantsSongs && m_searchTopResult)
            || m_searchFacets.Size() > 0;
        SearchEmptyPanel().Visibility(hasContent ? Visibility::Collapsed : Visibility::Visible);
        if (!hasContent)
        {
            auto const query = std::wstring(TrimQuery(GlobalSearchBox().Text()).c_str());
            SearchEmptyTitleText().Text(winrt::hstring(L"Nothing for “" + query + L"”"));
            SearchEmptyHintText().Text(m_searchScope == L"Library"
                ? L"Check the spelling, or search the full catalog instead of your library."
                : L"Check the spelling, or try a broader search.");
        }
    }

    void MainWindow::ApplyBrowseScopeVisuals()
    {
        EnsureAccentBrushes();
        auto const library = m_searchScope == L"Library";
        BrowseScopeLibraryButton().Background(library ? m_brushNeutralFill : m_brushTransparent);
        BrowseScopeCatalogButton().Background(library ? m_brushTransparent : m_brushNeutralFill);
        BrowseScopeLibraryButton().Foreground(library ? m_brushLabelIdle : m_brushTextTertiary);
        BrowseScopeCatalogButton().Foreground(library ? m_brushTextTertiary : m_brushLabelIdle);
    }

    void MainWindow::ApplyBrowseFilterVisuals()
    {
        struct FilterButton
        {
            winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton Button;
            wchar_t const* Name;
        };
        FilterButton const buttons[] = {
            { BrowseFilterAll(), L"All" },
            { BrowseFilterSongs(), L"Songs" },
            { BrowseFilterAlbums(), L"Albums" },
            { BrowseFilterArtists(), L"Artists" },
            { BrowseFilterPlaylists(), L"Playlists" }
        };
        for (auto const& row : buttons)
        {
            row.Button.IsChecked(m_searchFilter == row.Name);
        }
    }

    void MainWindow::LoadPersistedRecentSearches()
    {
        m_recentSearches = LastMusicPlayer::Backend::DecodeRecentSearches(
            std::wstring(SettingsManagerService().GetString(L"RecentSearches", L"").c_str()));
    }

    void MainWindow::RecordBrowseRecentSearch(winrt::hstring const& query)
    {
        m_recentSearches = LastMusicPlayer::Backend::RecordRecentSearch(
            m_recentSearches,
            std::wstring(query.c_str()));
        SettingsManagerService().SetString(
            L"RecentSearches",
            winrt::hstring(LastMusicPlayer::Backend::EncodeRecentSearches(m_recentSearches)));
        RebuildBrowseRecentChips();
    }

    void MainWindow::RebuildBrowseRecentChips()
    {
        auto panel = BrowseRecentSearchChips();
        if (!panel)
        {
            return;
        }
        panel.Children().Clear();
        for (auto const& query : m_recentSearches)
        {
            MUXC::Button button;
            button.Tag(winrt::box_value(winrt::hstring(query)));
            // White, not transparent: these chips sit on the page fill, and the
            // reference reads them as cards on it rather than outlines cut into it.
            button.Background(m_brushSurface);
            button.BorderBrush(m_brushStroke);
            button.BorderThickness({ 1 });
            button.CornerRadius({ 999 });
            button.Padding({ 14, 7, 14, 7 });
            button.FontSize(13);

            MUXC::StackPanel content;
            content.Orientation(MUXC::Orientation::Horizontal);
            content.Spacing(9);
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
            ? Visibility::Collapsed
            : Visibility::Visible);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateBrowseLandingAsync(bool force)
    {
        auto lifetime = get_strong();
        if (m_browseLandingLoaded && !force)
        {
            co_return;
        }

        auto const epoch = ++m_browseLandingEpoch;
        auto dispatcher = DispatcherQueue();
        std::vector<winrt::Last_Music_Player::TrackInfo> genres;
        std::vector<winrt::Last_Music_Player::TrackInfo> artists;
        std::vector<std::wstring> rankedGenres;
        std::vector<winrt::Last_Music_Player::TrackInfo> history;

        co_await winrt::resume_background();
        if (DatabaseService().IsInitialized())
        {
            genres = DatabaseService().LoadGenres();
            artists = DatabaseService().LoadArtists();
            rankedGenres = DatabaseService().LoadTopGenres(
                static_cast<int>(LastMusicPlayer::Backend::kBrowseLandingLabelLimit));
            history = DatabaseService().LoadHistoryTracks(true, true);
        }

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_browseLandingEpoch)
        {
            co_return;
        }

        std::unordered_map<std::wstring, winrt::Last_Music_Player::TrackInfo> byGenre;
        for (auto const& genre : genres)
        {
            byGenre.emplace(ToLowerCopy(genre.Title()), genre);
        }

        // LoadArtists already orders by track count, so its head is the ranking
        // BrowseLandingLabels wants for a library whose files carry no genre tag.
        std::unordered_map<std::wstring, winrt::Last_Music_Player::TrackInfo> byArtist;
        std::vector<std::wstring> rankedArtists;
        for (auto const& artist : artists)
        {
            byArtist.emplace(ToLowerCopy(artist.Title()), artist);
            if (rankedArtists.size() < LastMusicPlayer::Backend::kBrowseLandingLabelLimit)
            {
                rankedArtists.emplace_back(artist.Title().c_str());
            }
        }

        // A tile has to lead somewhere, so each label is kept only when the
        // library actually holds the group behind it. That also filters out the
        // policy's static last-resort labels ("Pop", "Chill", ...), which name
        // genres an empty library does not have: on a library with nothing to
        // browse the section stays hidden rather than offering tiles that open
        // an empty page.
        auto const appendCategory = [this](
            winrt::Last_Music_Player::TrackInfo const& group,
            winrt::hstring const& context)
        {
            auto category = group;
            category.ArtworkCaption(winrt::hstring(
                std::to_wstring(category.TrackCount())
                + (category.TrackCount() == 1 ? L" track" : L" tracks")));
            ResolveArtworkPresentation(category, context);
            m_browseCategories.Append(category);
        };

        m_browseCategories.Clear();
        for (auto const& name : LastMusicPlayer::Backend::BrowseLandingLabels(rankedGenres, rankedArtists))
        {
            auto const folded = ToLowerCopy(winrt::hstring(name));
            if (auto const found = byGenre.find(folded); found != byGenre.end())
            {
                appendCategory(found->second, L"genre");
                continue;
            }
            if (auto const found = byArtist.find(folded); found != byArtist.end())
            {
                appendCategory(found->second, L"artist");
            }
        }
        if (m_browseCategories.Size() == 0)
        {
            // LoadTopGenres drops broad media categories that are not musical
            // genres, so a library tagged only with those ranks empty while
            // still having genres worth showing.
            for (auto const& genre : genres)
            {
                if (m_browseCategories.Size() >= LastMusicPlayer::Backend::kBrowseLandingLabelLimit)
                {
                    break;
                }
                appendCategory(genre, L"genre");
            }
        }

        m_browseResumeTracks.Clear();
        for (auto const& source : history)
        {
            if (m_browseResumeTracks.Size() >= kBrowseResumeLimit || !IsPlayableHomeTrack(source))
            {
                continue;
            }
            auto track = source;
            ResolveArtworkPresentation(track, L"track");
            m_browseResumeTracks.Append(track);
        }
        BrowseResumeSection().Visibility(m_browseResumeTracks.Size() == 0
            ? Visibility::Collapsed
            : Visibility::Visible);
        m_browseLandingLoaded = true;
    }
}
