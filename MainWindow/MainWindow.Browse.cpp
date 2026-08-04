#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/TrackSearchPolicy.h"

#include <algorithm>
#include <string>
#include <vector>

namespace winrt::Last_Music_Player::implementation
{
    void MainWindow::BrowseButton_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        ShowPrimaryView(L"Browse");

        auto const query = detail::TrimQuery(GlobalSearchBox().Text());
        if (query.size() < LastMusicPlayer::Backend::kMinimumSearchQueryLength)
        {
            ShowBrowseLanding(query.size() == 1);
        }
        detail::RunDetached(HydrateBrowseLandingAsync(false));
    }

    void MainWindow::HomeSearchEntry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        BrowseButton_Click(sender, args);
        GlobalSearchBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
    }

    void MainWindow::HomeSearchEntry_Tapped(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        (void)sender;
        ShowPrimaryView(L"Browse");
        auto const query = detail::TrimQuery(GlobalSearchBox().Text());
        if (query.size() < LastMusicPlayer::Backend::kMinimumSearchQueryLength)
        {
            ShowBrowseLanding(query.size() == 1);
        }
        detail::RunDetached(HydrateBrowseLandingAsync(false));
        GlobalSearchBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        args.Handled(true);
    }

    void MainWindow::BrowseCategory_ItemClick(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto category = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!category || category.Title().empty())
        {
            return;
        }

        GlobalSearchBox().Text(category.Title());
        GlobalSearchBox().Focus(winrt::Microsoft::UI::Xaml::FocusState::Programmatic);
        GlobalSearchBox().Select(static_cast<int32_t>(category.Title().size()), 0);
    }

    void MainWindow::SearchListView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSearchGridMode(false);
    }

    void MainWindow::SearchGridView_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        SetSearchGridMode(true);
    }

    void MainWindow::SearchSort_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
        if (!item)
        {
            return;
        }

        auto const sort = LastMusicPlayer::Backend::ParseSearchResultSort(
            detail::ReadTagString(item.Tag()).c_str());
        auto const tag = LastMusicPlayer::Backend::SearchResultSortTag(sort);
        m_searchSort.assign(tag.begin(), tag.end());
        SearchSortLabel().Text(winrt::hstring(
            LastMusicPlayer::Backend::SearchResultSortLabel(sort)));
        ApplySearchResultSort();
    }

    void MainWindow::SearchRetry_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        detail::RunDetached(RunHomeSearchAsync());
    }

    void MainWindow::ShowBrowseLanding(bool showMinimumLengthHint)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        BrowseLandingPanel().Visibility(Visibility::Visible);
        BrowseSearchPanel().Visibility(Visibility::Collapsed);
        BrowseSearchHintText().Visibility(
            showMinimumLengthHint ? Visibility::Visible : Visibility::Collapsed);
        SearchStatusText().Text(L"Browse");
        m_searchSkeleton.EndLoading();
    }

    void MainWindow::ShowBrowseSearchLoading()
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Visible);
        SearchErrorPanel().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        SearchResultsListView().Visibility(Visibility::Collapsed);
        SearchSongsListView().Visibility(Visibility::Collapsed);
        SearchResultCountText().Text(L"0 results");
        SearchStatusText().Text(L"Searching");
    }

    void MainWindow::ShowBrowseSearchResults(winrt::hstring const& query)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Collapsed);

        auto const total = m_searchAllResults.size();
        auto const countText = winrt::to_hstring(total)
            + (total == 1 ? L" result" : L" results");
        SearchResultCountText().Text(countText);
        SearchStatusText().Text(countText);

        auto const empty = total == 0;
        SearchEmptyPanel().Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        if (empty)
        {
            SearchEmptyTitleText().Text(L"No results for \"" + query + L"\"");
            SearchResultsListView().Visibility(Visibility::Collapsed);
            SearchSongsListView().Visibility(Visibility::Collapsed);
            return;
        }

        SetSearchGridMode(m_searchGridMode);
    }

    void MainWindow::ShowBrowseSearchError(winrt::hstring const& message)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        BrowseLandingPanel().Visibility(Visibility::Collapsed);
        BrowseSearchPanel().Visibility(Visibility::Visible);
        SearchLoadingText().Visibility(Visibility::Collapsed);
        SearchEmptyPanel().Visibility(Visibility::Collapsed);
        SearchErrorPanel().Visibility(Visibility::Visible);
        SearchErrorMessageText().Text(message);
        SearchResultCountText().Text(L"0 results");
        SearchStatusText().Text(L"Unavailable");
        SearchResultsListView().Visibility(Visibility::Collapsed);
        SearchSongsListView().Visibility(Visibility::Collapsed);
    }

    void MainWindow::SetSearchGridMode(bool gridMode)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;

        m_searchGridMode = gridMode;
        EnsureAccentBrushes();
        SearchListViewButton().Background(gridMode ? m_brushTransparent : m_brushAccentSoft);
        SearchGridViewButton().Background(gridMode ? m_brushAccentSoft : m_brushTransparent);

        auto const canShowResults = !m_searchAllResults.empty()
            && SearchErrorPanel().Visibility() == Visibility::Collapsed
            && SearchEmptyPanel().Visibility() == Visibility::Collapsed
            && SearchLoadingText().Visibility() == Visibility::Collapsed;
        SearchResultsListView().Visibility(
            canShowResults && !gridMode ? Visibility::Visible : Visibility::Collapsed);
        SearchSongsListView().Visibility(
            canShowResults && gridMode ? Visibility::Visible : Visibility::Collapsed);
    }

    void MainWindow::ApplySearchResultSort()
    {
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

        auto const sort = LastMusicPlayer::Backend::ParseSearchResultSort(m_searchSort);
        auto const order = LastMusicPlayer::Backend::SearchResultOrder(keys, sort);

        m_searchTracks.Clear();
        int32_t index = 1;
        for (auto resultIndex : order)
        {
            auto track = m_searchAllResults[resultIndex];
            track.Index(index++);
            detail::ResolveArtworkPresentation(track, L"track");
            m_searchTracks.Append(track);
        }
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
        std::vector<std::wstring> rankedGenres;
        std::vector<std::wstring> rankedArtists;

        co_await winrt::resume_background();
        if (detail::DatabaseService().IsInitialized())
        {
            rankedGenres = detail::DatabaseService().LoadTopGenres(
                static_cast<int>(LastMusicPlayer::Backend::kBrowseLandingLabelLimit));
            if (rankedGenres.empty())
            {
                auto artists = detail::DatabaseService().LoadArtists();
                std::stable_sort(artists.begin(), artists.end(),
                    [](auto const& left, auto const& right)
                    {
                        if (left.TrackCount() != right.TrackCount())
                        {
                            return left.TrackCount() > right.TrackCount();
                        }
                        return detail::ToLowerCopy(left.Title())
                            < detail::ToLowerCopy(right.Title());
                    });
                for (auto const& artist : artists)
                {
                    auto const title = std::wstring(artist.Title().c_str());
                    if (title.empty() || detail::ToLowerCopy(artist.Title()) == L"unknown artist")
                    {
                        continue;
                    }
                    rankedArtists.push_back(title);
                    if (rankedArtists.size() >= LastMusicPlayer::Backend::kBrowseLandingLabelLimit)
                    {
                        break;
                    }
                }
            }
        }
        auto labels = LastMusicPlayer::Backend::BrowseLandingLabels(rankedGenres, rankedArtists);

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_browseLandingEpoch)
        {
            co_return;
        }

        m_browseCategories.Clear();
        for (auto const& label : labels)
        {
            winrt::Last_Music_Player::TrackInfo category;
            category.Title(winrt::hstring(label));
            category.SourceKind(L"browse-category");
            m_browseCategories.Append(category);
        }
        m_browseLandingLoaded = true;
    }
}
