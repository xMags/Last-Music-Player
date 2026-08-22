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

    void MainWindow::LoadCatalogFromDatabase()
    {
        if (DatabaseService().IsInitialized())
        {
            m_catalogTracks = DatabaseService().LoadTracks(true, true);
        }
        else
        {
            m_catalogTracks.assign(m_queue.CurrentPlaylist.begin(), m_queue.CurrentPlaylist.end());
        }

        m_catalogLoaded = true;
        m_songsResultsValid = false;
        RefreshLibraryCatalogViews();
        ApplySongsFilterSort();
    }

    void MainWindow::RefreshLibraryCatalogViews()
    {
        m_librarySongs.Clear();
        m_librarySongAllResults.clear();
        int index = 1;
        for (auto const& track : m_catalogTracks)
        {
            auto copy = track;
            copy.Index(index++);
            m_librarySongAllResults.push_back(copy);
        }
        AppendLibrarySongsPage();

        m_albums.Clear();
        m_artists.Clear();
        m_libraryGenres.Clear();
        m_manualPlaylists.Clear();
        m_sidebarPlaylists.Clear();

        if (DatabaseService().IsInitialized())
        {
            for (auto const& album : DatabaseService().LoadAlbums())
            {
                ResolveArtworkPresentation(album, L"album");
                m_albums.Append(album);
            }
            for (auto const& artist : DatabaseService().LoadArtists())
            {
                ResolveArtworkPresentation(artist, L"artist");
                m_artists.Append(artist);
            }
            for (auto const& genre : DatabaseService().LoadGenres())
            {
                ResolveArtworkPresentation(genre, L"genre");
                m_libraryGenres.Append(genre);
            }
            for (auto const& playlist : DatabaseService().LoadPlaylists())
            {
                ResolveArtworkPresentation(playlist, L"playlist");
                m_manualPlaylists.Append(playlist);
            }
            for (auto const& playlist : DatabaseService().LoadRecentPlaylists(4))
            {
                ResolveArtworkPresentation(playlist, L"playlist");
                m_sidebarPlaylists.Append(playlist);
            }
        }

        RefreshAutoPlaylists();
        UpdateLibraryActionButtons();
    }

    void MainWindow::RefreshAutoPlaylists()
    {
        m_yourPlaylists.Clear();
        m_autoPlaylists.Clear();

        // A cover the three system playlists carry instead of the palette
        // gradient that LibraryArtworkBorder_Loaded hashes out of the item
        // identity. That hash is fine for spreading colour across a wall of
        // mixes, but it has no idea which three tiles are the fixed landmarks,
        // and it happened to hand Favourites and Most Played two greens that
        // are hard to tell apart.
        //
        // Size and opacity are per cover on purpose: Segoe Fluent Icons draws
        // the heart filled but the flame and the clock as thin outlines, so a
        // single pair of values would leave the outlines much fainter than the
        // heart at the same nominal size.
        struct CoverDef
        {
            wchar_t const* Glyph;
            double GlyphSize;
            double GlyphOpacity;
            winrt::Windows::UI::Color GradientStart;
            winrt::Windows::UI::Color GradientEnd;
        };

        static constexpr CoverDef kFavouritesCover{
            L"\xEB52", 40.0, 0.50, { 255, 0xFF, 0x6B, 0xD0 }, { 255, 0xC0, 0x4B, 0xFE } };
        static constexpr CoverDef kMostPlayedCover{
            L"\xECAD", 46.0, 0.85, { 255, 0xFF, 0xA0, 0x42 }, { 255, 0xFF, 0x3B, 0x6A } };
        static constexpr CoverDef kRecentlyAddedCover{
            L"\xE823", 46.0, 0.85, { 255, 0x14, 0xA8, 0xBC }, { 255, 0x7F, 0x6C, 0xFF } };

        // Runs on the UI thread, as the whole of RefreshAutoPlaylists does: it
        // is only ever reached from a dispatched callback or from a coroutine
        // already touching the bound collections, and a Brush carries the same
        // thread affinity as any other XAML object.
        auto makeCoverBrush = [](CoverDef const& cover)
        {
            winrt::Microsoft::UI::Xaml::Media::LinearGradientBrush brush;
            brush.StartPoint({ 0.0f, 0.0f });
            brush.EndPoint({ 1.0f, 1.0f });
            winrt::Microsoft::UI::Xaml::Media::GradientStop start;
            start.Color(cover.GradientStart);
            start.Offset(0.0);
            brush.GradientStops().Append(start);
            winrt::Microsoft::UI::Xaml::Media::GradientStop end;
            end.Color(cover.GradientEnd);
            end.Offset(1.0);
            brush.GradientStops().Append(end);
            return brush;
        };

        struct AutoPlaylistDef
        {
            wchar_t const* Key;
            wchar_t const* Title;
            wchar_t const* Caption;
            // Null for the generated mixes, which keep the hashed palette
            // gradient and show no centre glyph.
            CoverDef const* Cover;
        };

        // The gradients match the ones Home already paints behind these three
        // sections, so a playlist keeps one colour identity across both views.
        static constexpr AutoPlaylistDef kSystemPlaylists[] = {
            { L"smart-liked", L"Favourites", L"Every song you've liked.", &kFavouritesCover },
            { L"smart-most", L"Most Played", L"Your most-played songs.", &kMostPlayedCover },
            { L"smart-recent", L"Recently Added", L"The newest songs in your library.", &kRecentlyAddedCover },
        };
        static constexpr AutoPlaylistDef kAutoMixes[] = {
            { L"daily1", L"Daily Mix 1", L"Built from recent listening.", nullptr },
            { L"daily2", L"Daily Mix 2", L"A second lane through your library.", nullptr },
            { L"daily3", L"Daily Mix 3", L"Balanced across your artists.", nullptr },
            { L"daily4", L"Daily Mix 4", L"More from a genre you love.", nullptr },
            { L"daily5", L"Daily Mix 5", L"Another route through your favourites.", nullptr },
            { L"repeat", L"On Repeat", L"Songs you keep coming back to.", nullptr },
            { L"discover", L"Discover Weekly", L"A fresh pass through your tracks.", nullptr },
            { L"timecapsule", L"Time Capsule", L"Older additions worth revisiting.", nullptr },
            { L"fresh", L"Fresh Finds", L"Your newest library additions.", nullptr },
        };

        auto appendGenerated = [this, &makeCoverBrush](
            AutoPlaylistDef const& def,
            winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> const& target,
            wchar_t const* kindLabel,
            wchar_t const* sourceLabel)
        {
            auto mixIt = m_homeMixes.find(def.Key);
            auto count = mixIt == m_homeMixes.end()
                ? 0
                : static_cast<int32_t>(mixIt->second.size());
            auto summary = std::to_wstring(count);
            summary += count == 1 ? L" song - " : L" songs - ";
            summary += kindLabel;

            LastMusicPlayer::Backend::TrackInfo playlist;
            playlist.Title(def.Title);
            playlist.Artist(winrt::hstring(summary));
            playlist.SourceKind(L"auto-playlist");
            playlist.Provider(L"auto");
            playlist.SourceUrl(def.Key);
            playlist.SourceLabel(sourceLabel);
            playlist.TrackCount(count);
            playlist.ArtworkCaption(def.Caption);
            ResolveArtworkPresentation(playlist, L"auto-playlist");
            // After the shared resolve, which fills in the default glyph only
            // when a caller has not already chosen one.
            if (def.Cover)
            {
                playlist.ArtworkGlyph(def.Cover->Glyph);
                playlist.ArtworkGlyphSize(def.Cover->GlyphSize);
                playlist.ArtworkGlyphOpacity(def.Cover->GlyphOpacity);
                playlist.ArtworkBackground(makeCoverBrush(*def.Cover));
            }
            target.Append(playlist);
        };

        for (auto const& def : kSystemPlaylists)
        {
            appendGenerated(def, m_yourPlaylists, L"System playlist", L"System");
        }
        for (uint32_t index = 0; index < m_manualPlaylists.Size(); ++index)
        {
            m_yourPlaylists.Append(m_manualPlaylists.GetAt(index));
        }
        for (auto const& def : kAutoMixes)
        {
            appendGenerated(def, m_autoPlaylists, L"Auto mix", L"Auto mix");
        }
    }


    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateLibraryTabAsync(winrt::hstring tab, bool force)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        auto key = std::wstring(tab.c_str());

        auto shouldLoad = [&](LoadState state)
        {
            return force || state == LoadState::NotLoaded || state == LoadState::Dirty;
        };
        auto markStaleLoad = [](LoadState& state)
        {
            if (state == LoadState::Loading)
            {
                state = LoadState::Dirty;
            }
        };

        if (key == L"History" || key == L"MostPlayed")
        {
            if (!shouldLoad(m_librarySongsState) && !m_librarySongsPageLoading)
            {
                co_return;
            }
            auto epoch = ++m_libraryHydrationEpoch;
            m_librarySongsPageLoading = false;
            ++m_librarySongsPageLoadId;
            m_librarySongsState = LoadState::Loading;
            m_librarySongAllResults.clear();
            m_librarySongs.Clear();
            m_librarySongsMatchedCount = 0;
            m_librarySongsMatchedSeconds = 0.0;
            UpdateHistoryCount();
            // LibraryImportStatusText still carries scan and import progress,
            // so it is only the plain "loading" messages that the placeholder
            // takes over from.
            BeginLibraryTabSkeleton(m_libraryHistoryGridMode
                ? LastMusicPlayer::Frontend::SkeletonShape::TileGrid
                : LastMusicPlayer::Frontend::SkeletonShape::TrackList);
            co_await AppendLibrarySongsPageAsync();
            if (epoch == m_libraryHydrationEpoch)
            {
                m_librarySongsState = LoadState::Loaded;
                LibraryImportStatusText().Text(L"");
                m_libraryTabsSkeleton.EndLoading();
            }
            else
            {
                markStaleLoad(m_librarySongsState);
            }
            co_return;
        }

        if (key == L"Playlists")
        {
            if (!shouldLoad(m_libraryPlaylistsState))
            {
                co_return;
            }
            auto epoch = ++m_libraryHydrationEpoch;
            m_libraryPlaylistsState = LoadState::Loading;
            BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape::TileGrid);
            auto scope = m_libraryScope;
            auto remoteScope = RemoteMusicServiceService().CaptureScope();
            auto accountSnapshot = AccountSessionService().Snapshot();
            std::wstring accountOwnerId;
            if (remoteScope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account
                && remoteScope.AccountGeneration == accountSnapshot.Generation
                && (accountSnapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                    || accountSnapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Offline))
            {
                accountOwnerId = std::wstring(accountSnapshot.Profile.Id.c_str());
            }
            std::vector<winrt::Last_Music_Player::TrackInfo> smartLiked;
            std::vector<winrt::Last_Music_Player::TrackInfo> smartMostPlayed;
            std::vector<winrt::Last_Music_Player::TrackInfo> smartRecentlyAdded;
            co_await winrt::resume_background();
            std::vector<winrt::Last_Music_Player::TrackInfo> playlists;
            std::vector<winrt::Last_Music_Player::TrackInfo> sidebar;
            if (DatabaseService().IsInitialized())
            {
                playlists = DatabaseService().LoadPlaylists();
                sidebar = DatabaseService().LoadRecentPlaylists(4);
                LastMusicPlayer::Backend::TrackQuery query;
                query.Scope = scope;
                query.Filter = L"Liked";
                query.Sort = L"DateAdded";
                smartLiked = DatabaseService().LoadTracksForQuery(query);
                query.Filter = L"Most";
                query.Sort = L"MostPlayed";
                smartMostPlayed = DatabaseService().LoadTracksForQuery(query);
                query.Filter = L"All";
                query.Sort = L"DateAdded";
                smartRecentlyAdded = DatabaseService().LoadTracksForQuery(query);
            }
            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_libraryHydrationEpoch)
            {
                markStaleLoad(m_libraryPlaylistsState);
                co_return;
            }
            auto accountScopeCurrent = !accountOwnerId.empty()
                && DatabaseService().ActiveAccountId() == accountOwnerId
                && RemoteMusicServiceService().IsCurrent(remoteScope);
            if (scope == L"Account" && !accountScopeCurrent)
            {
                markStaleLoad(m_libraryPlaylistsState);
                co_return;
            }

            m_accountPlaylistBindings.clear();
            m_manualPlaylists.Clear();
            for (auto const& playlist : playlists)
            {
                auto accountPlaylist = playlist.Provider() == L"account";
                if ((scope == L"Account" && !accountPlaylist)
                    || (scope == L"OnThisPc" && accountPlaylist)
                    || (accountPlaylist && !accountScopeCurrent))
                {
                    continue;
                }
                auto copy = playlist;
                ResolveArtworkPresentation(copy, L"playlist");
                m_manualPlaylists.Append(copy);
                if (accountPlaylist)
                {
                    BindAccountPlaylist(copy, remoteScope, accountOwnerId);
                }
            }
            m_sidebarPlaylists.Clear();
            for (auto const& playlist : sidebar)
            {
                auto accountPlaylist = playlist.Provider() == L"account";
                if (accountPlaylist && !accountScopeCurrent)
                {
                    continue;
                }
                auto copy = playlist;
                ResolveArtworkPresentation(copy, L"playlist");
                m_sidebarPlaylists.Append(copy);
                if (accountPlaylist)
                {
                    BindAccountPlaylist(copy, remoteScope, accountOwnerId);
                }
            }
            auto prepareSmart = [this](std::vector<winrt::Last_Music_Player::TrackInfo>& tracks)
            {
                for (auto& track : tracks)
                {
                    ResolveArtworkPresentation(track, L"track");
                }
            };
            prepareSmart(smartLiked);
            prepareSmart(smartMostPlayed);
            prepareSmart(smartRecentlyAdded);
            m_homeMixes[L"smart-liked"] = std::move(smartLiked);
            m_homeMixes[L"smart-most"] = std::move(smartMostPlayed);
            m_homeMixes[L"smart-recent"] = std::move(smartRecentlyAdded);
            RefreshAutoPlaylists();
            m_libraryPlaylistsState = LoadState::Loaded;
            UpdateLibraryHeaderMetrics();
            LibraryImportStatusText().Text(L"");
            m_libraryTabsSkeleton.EndLoading();
            co_return;
        }

        if (key == L"Albums")
        {
            if (!shouldLoad(m_libraryAlbumsState))
            {
                co_return;
            }
            auto epoch = ++m_libraryHydrationEpoch;
            m_libraryAlbumsState = LoadState::Loading;
            BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape::TileGrid);
            co_await winrt::resume_background();
            auto albums = DatabaseService().IsInitialized() ? DatabaseService().LoadAlbums() : std::vector<winrt::Last_Music_Player::TrackInfo>{};
            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_libraryHydrationEpoch)
            {
                markStaleLoad(m_libraryAlbumsState);
                co_return;
            }
            m_albums.Clear();
            for (auto const& album : albums)
            {
                auto copy = album;
                ResolveArtworkPresentation(copy, L"album");
                m_albums.Append(copy);
            }
            m_libraryAlbumsState = LoadState::Loaded;
            UpdateLibraryHeaderMetrics();
            LibraryImportStatusText().Text(L"");
            m_libraryTabsSkeleton.EndLoading();
            co_return;
        }

        if (key == L"Artists")
        {
            if (!shouldLoad(m_libraryArtistsState))
            {
                co_return;
            }
            auto epoch = ++m_libraryHydrationEpoch;
            m_libraryArtistsState = LoadState::Loading;
            BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape::TileGrid);
            co_await winrt::resume_background();
            auto artists = DatabaseService().IsInitialized() ? DatabaseService().LoadArtists() : std::vector<winrt::Last_Music_Player::TrackInfo>{};
            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_libraryHydrationEpoch)
            {
                markStaleLoad(m_libraryArtistsState);
                co_return;
            }
            m_artists.Clear();
            for (auto const& artist : artists)
            {
                auto copy = artist;
                ResolveArtworkPresentation(copy, L"artist");
                m_artists.Append(copy);
            }
            m_libraryArtistsState = LoadState::Loaded;
            UpdateLibraryHeaderMetrics();
            LibraryImportStatusText().Text(L"");
            m_libraryTabsSkeleton.EndLoading();
            co_return;
        }

        if (key == L"Genres")
        {
            if (!shouldLoad(m_libraryGenresState))
            {
                co_return;
            }
            auto epoch = ++m_libraryHydrationEpoch;
            m_libraryGenresState = LoadState::Loading;
            BeginLibraryTabSkeleton(LastMusicPlayer::Frontend::SkeletonShape::TileGrid);
            co_await winrt::resume_background();
            auto genres = DatabaseService().IsInitialized() ? DatabaseService().LoadGenres() : std::vector<winrt::Last_Music_Player::TrackInfo>{};
            co_await wil::resume_foreground(dispatcher);
            if (epoch != m_libraryHydrationEpoch)
            {
                markStaleLoad(m_libraryGenresState);
                co_return;
            }
            m_libraryGenres.Clear();
            for (auto const& genre : genres)
            {
                auto copy = genre;
                ResolveArtworkPresentation(copy, L"genre");
                m_libraryGenres.Append(copy);
            }
            m_libraryGenresState = LoadState::Loaded;
            UpdateLibraryHeaderMetrics();
            LibraryImportStatusText().Text(L"");
            m_libraryTabsSkeleton.EndLoading();
        }
    }


    void MainWindow::AppendLibrarySongsPage()
    {
        AppendTrackPage(
            m_librarySongAllResults,
            m_librarySongs,
            m_libraryHistoryGridMode ? kSongsGridPageSize : kLibrarySongPageSize);
        UpdateHistoryCount();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AppendLibrarySongsPageAsync()
    {
        auto lifetime = get_strong();
        if (m_librarySongsPageLoading)
        {
            co_return;
        }
        if (m_librarySongsMatchedCount > 0 && static_cast<int>(m_librarySongAllResults.size()) >= m_librarySongsMatchedCount)
        {
            co_return;
        }

        auto dispatcher = this->DispatcherQueue();
        auto epoch = m_libraryHydrationEpoch;
        auto offset = static_cast<uint32_t>(m_librarySongAllResults.size());
        auto pageSize = m_libraryHistoryGridMode ? kSongsGridPageSize : kLibrarySongPageSize;
        auto query = CurrentLibrarySongsQuery(offset, pageSize);
        m_librarySongsPageLoading = true;
        auto pageLoadId = ++m_librarySongsPageLoadId;

        co_await winrt::resume_background();
        auto page = DatabaseService().LoadTrackPage(query);

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_libraryHydrationEpoch || pageLoadId != m_librarySongsPageLoadId)
        {
            if (pageLoadId == m_librarySongsPageLoadId)
            {
                m_librarySongsPageLoading = false;
            }
            co_return;
        }

        m_librarySongsMatchedCount = page.TotalCount;
        m_librarySongsMatchedSeconds = page.TotalSeconds;
        for (auto const& source : page.Tracks)
        {
            auto copy = source;
            copy.Index(static_cast<int32_t>(m_librarySongAllResults.size() + 1));
            ResolveArtworkPresentation(copy, L"track");
            m_librarySongAllResults.push_back(copy);
            m_librarySongs.Append(copy);
        }
        if (pageLoadId == m_librarySongsPageLoadId)
        {
            m_librarySongsPageLoading = false;
        }
        UpdateHistoryCount();
    }


    void MainWindow::MaybeAppendLibrarySongsPage(uint32_t itemIndex)
    {
        auto total = m_librarySongsMatchedCount > 0 ? static_cast<uint32_t>(m_librarySongsMatchedCount) : static_cast<uint32_t>(m_librarySongAllResults.size());
        if (m_librarySongs.Size() < total &&
            itemIndex + kPageAppendThreshold >= m_librarySongs.Size())
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


    void MainWindow::MarkLibraryViewsDirty()
    {
        m_songsLoadState = LoadState::Dirty;
        m_songsResultsValid = false;
        m_libraryAlbumsState = LoadState::Dirty;
        m_libraryArtistsState = LoadState::Dirty;
        m_librarySongsState = LoadState::Dirty;
        m_libraryGenresState = LoadState::Dirty;
        m_libraryPlaylistsState = LoadState::Dirty;
        m_libraryDetailState = LoadState::Dirty;
        m_browseLandingLoaded = false;
        ++m_browseLandingEpoch;
    }


    LastMusicPlayer::Backend::TrackQuery MainWindow::CurrentLibrarySongsQuery(uint32_t offset, uint32_t limit) const
    {
        LastMusicPlayer::Backend::TrackQuery query;
        query.Filter = m_libraryTracksFilter;
        query.Sort = m_libraryHistorySort;
        query.Scope = m_libraryScope;
        query.Offset = static_cast<int>(offset);
        query.Limit = static_cast<int>(limit);
        query.IncludeRemote = true;
        query.ActiveOnly = true;
        return query;
    }

    LastMusicPlayer::Backend::TrackQuery MainWindow::CurrentLibraryDetailQuery(
        uint32_t offset,
        uint32_t limit,
        std::wstring const& accountOwnerId) const
    {
        LastMusicPlayer::Backend::TrackQuery query;
        query.Filter = L"All";
        query.Sort = LastMusicPlayer::Backend::DetailSortQueryValue(m_libraryDetailSort);
        query.GroupKind = m_libraryDetailKind;
        query.SearchText = m_libraryDetailFindText;
        query.GroupKey = m_libraryDetailKey;
        query.AccountOwnerId = accountOwnerId;
        query.Offset = static_cast<int>(offset);
        query.Limit = static_cast<int>(limit);
        query.IncludeRemote = true;
        query.ActiveOnly = true;
        return query;
    }



    void MainWindow::LibrarySongs_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        QueueContainerArtwork(args);
        if (!args.InRecycleQueue())
        {
            MaybeAppendLibrarySongsPage(args.ItemIndex());
        }
    }

}
