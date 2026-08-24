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
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        winrt::hstring LibraryArtistDuration(double seconds)
        {
            auto const whole = static_cast<std::int64_t>((std::max)(0.0, seconds));
            wchar_t value[32]{};
            swprintf_s(value, L"%lld:%02lld", whole / 60, whole % 60);
            return value;
        }

        winrt::Last_Music_Player::TrackInfo TrackFromLibraryArtistDto(
            LastMusicPlayer::Backend::AccountLibraryTrackDto const& item)
        {
            auto const& value = item.Track;
            winrt::Last_Music_Player::TrackInfo track;
            track.RemoteId(winrt::hstring(value.Id));
            track.Title(winrt::hstring(value.Title));
            track.Artist(winrt::hstring(value.Artist));
            track.Album(winrt::hstring(value.Album));
            track.Genre(winrt::hstring(value.Genre));
            track.ArtworkUrl(winrt::hstring(value.ArtworkUrl));
            track.SourceKind(winrt::hstring(value.SourceKind));
            track.Provider(L"account");
            track.RemoteProvider(winrt::hstring(value.Provider));
            track.SourceUrl(winrt::hstring(value.SourceUrl));
            track.RemoteCatalogId(winrt::hstring(value.CatalogId));
            track.AlbumCatalogId(winrt::hstring(value.AlbumCatalogId));
            track.ArtistCatalogId(winrt::hstring(value.ArtistCatalogId));
            track.DurationSeconds(value.DurationSec);
            track.Duration(LibraryArtistDuration(value.DurationSec));
            track.DateAdded(winrt::hstring(item.AddedAt));
            track.ResumePositionSeconds(item.LastPositionSec);
            track.IsLiked(item.Liked);
            track.SourceLabel(L"Account");
            detail::ApplyMusicArtwork(track, track.ArtworkUrl(), L"track");
            return track;
        }
    }

    void MainWindow::UpdateLibraryActionButtons()
    {
        using V = winrt::Microsoft::UI::Xaml::Visibility;

        auto isChecked = [](winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button)
        {
            if (!button)
            {
                return false;
            }
            auto value = button.IsChecked();
            return value && value.Value();
        };

        auto detailVisible = LibraryDetailContent() && LibraryDetailContent().Visibility() == V::Visible;
        auto playlistsActive = isChecked(LibTabPlaylists());
        auto albumsActive = isChecked(LibTabAlbums());
        auto artistsActive = isChecked(LibTabArtists());
        auto offlineActive = isChecked(LibTabSongs());
        auto historyActive = isChecked(LibTabHistory());
        auto mostPlayedActive = isChecked(LibTabMostPlayed());
        auto accountMode = RemoteMusicServiceService().Mode()
            == LastMusicPlayer::Backend::RemoteAccessMode::Account;

        if (LibraryCreateAlbumButton())
        {
            LibraryCreateAlbumButton().Visibility(V::Collapsed);
        }
        if (LibraryImportAlbumButton())
        {
            LibraryImportAlbumButton().Visibility(V::Collapsed);
        }
        if (LibraryCreatePlaylistButton())
        {
            auto visible = playlistsActive || historyActive || mostPlayedActive;
            LibraryCreatePlaylistButton().Visibility(!detailVisible && visible ? V::Visible : V::Collapsed);
            LibraryCreatePlaylistButton().IsEnabled(true);
        }
        if (LibraryImportPlaylistButton())
        {
            auto visible = playlistsActive || albumsActive || offlineActive;
            LibraryImportPlaylistButton().Visibility(!detailVisible && visible ? V::Visible : V::Collapsed);
            LibraryImportPlaylistButton().IsEnabled(!accountMode);
        }
        if (LibraryFollowArtistButton())
        {
            LibraryFollowArtistButton().Visibility(!detailVisible && artistsActive ? V::Visible : V::Collapsed);
        }
        if (LibraryAddFolderButton())
        {
            LibraryAddFolderButton().Visibility(detailVisible ? V::Collapsed : V::Visible);
        }
    }


    void MainWindow::LibraryPlaylistCardMenu_Opening(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Windows::Foundation::IInspectable const& args)
    {
        (void)args;
        auto menu = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyout>();
        if (!menu)
        {
            return;
        }

        winrt::Last_Music_Player::TrackInfo playlist{ nullptr };
        for (auto const& item : menu.Items())
        {
            auto action = item.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
            if (!action)
            {
                continue;
            }
            playlist = action.CommandParameter().try_as<winrt::Last_Music_Player::TrackInfo>();
            if (playlist)
            {
                break;
            }
        }

        auto visibility = playlist && playlist.SourceKind() == L"playlist"
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
        for (auto const& item : menu.Items())
        {
            auto element = item.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
            if (element && ReadTagString(element.Tag()) == L"EditablePlaylistAction")
            {
                element.Visibility(visibility);
            }
        }
    }

    // Reordering a track and removing it only mean something inside a playlist
    // the user owns. The same detail list also backs albums, artists, genres
    // and the generated playlists, where the handlers below already refuse the
    // work; hiding the actions there stops the menu from offering three items
    // that do nothing. Editable covers both the local playlists and the
    // account's own, which is what m_libraryDetailKind == "playlist" means.
    void MainWindow::LibraryDetailRowMenu_Opening(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Windows::Foundation::IInspectable const& args)
    {
        (void)args;
        auto menu = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyout>();
        if (!menu)
        {
            return;
        }

        auto visibility = m_libraryDetailKind == L"playlist"
            ? winrt::Microsoft::UI::Xaml::Visibility::Visible
            : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
        static constexpr std::wstring_view playlistActionTag{ L"PlaylistTrackAction" };
        for (auto const& item : menu.Items())
        {
            auto element = item.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>();
            if (!element)
            {
                continue;
            }
            auto tag = ReadTagString(element.Tag());
            if (std::wstring_view{ tag }.starts_with(playlistActionTag))
            {
                element.Visibility(visibility);
            }
        }
    }


    void MainWindow::LibraryRowMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto track = TrackFromActionSender(sender);
        if (!track)
        {
            return;
        }

        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
        {
            RunDetached(LoadLibraryDetailQueueAndPlayAsync(track));
        }
        else
        {
            RunDetached(LoadLibrarySongsQueueAndPlayAsync(track));
        }
    }

    void MainWindow::LibraryRowMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto track = TrackFromActionSender(sender);
        if (!track)
        {
            return;
        }

        if (!AudioPlayerService().GetCurrentTrack())
        {
            if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible)
            {
                RunDetached(LoadLibraryDetailQueueAndPlayAsync(track));
            }
            else
            {
                RunDetached(LoadLibrarySongsQueueAndPlayAsync(track));
            }
            return;
        }

        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible
            && IsAccountPlaylistDetail()
            && !CaptureAccountPlaylistDetailContext())
        {
            HideLibraryDetail();
            return;
        }
        PlayNextFromSongs(track);
    }

    void MainWindow::LibraryRowMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto track = TrackFromActionSender(sender);
        if (!track || !IsPlayableHomeTrack(track))
        {
            return;
        }
        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible
            && IsAccountPlaylistDetail()
            && !CaptureAccountPlaylistDetailContext())
        {
            HideLibraryDetail();
            return;
        }

        if (!AudioPlayerService().GetCurrentTrack() && m_queue.Queue.empty())
        {
            auto key = CatalogSourceKey(track);
            if (key.empty())
            {
                return;
            }

            m_queue.Queue.push_back(track);
            m_queue.QueueIndex = -1;
            RebuildUpNextQueue();
            return;
        }

        AddSongsTrackToQueue(track);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryRowMenuAddToPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        co_await AddTrackToPlaylistAsync(TrackFromActionSender(sender));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryRowMenuRemoveFromPlaylist_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        if (m_libraryDetailKind != L"playlist" || m_libraryDetailKey.empty() || !DatabaseService().IsInitialized())
        {
            co_return;
        }

        auto track = TrackFromActionSender(sender);
        auto title = LibraryDetailTitleText() ? LibraryDetailTitleText().Text() : winrt::hstring{};
        auto subtitle = m_libraryDetailSubtitle;
        static constexpr wchar_t accountPrefix[] = L"account-playlist|";
        if (m_libraryDetailKey.starts_with(accountPrefix))
        {
            auto accountContext = CaptureAccountPlaylistDetailContext();
            if (!track
                || track.RemoteId().empty()
                || !accountContext)
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }
            auto tracks = DatabaseService().LoadAccountPlaylistTracks(
                accountContext->Binding.OwnerId,
                accountContext->PlaylistId);
            std::vector<winrt::Last_Music_Player::TrackInfo> remainingTracks;
            remainingTracks.reserve(tracks.size());
            for (auto const& item : tracks)
            {
                if (!item.RemoteId().empty() && item.RemoteId() != track.RemoteId())
                {
                    remainingTracks.push_back(item);
                }
            }
            try
            {
                auto body = BuildAccountPlaylistUpdateJson(title, remainingTracks);
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    accountContext->Binding.Scope,
                    winrt::hstring(accountContext->PlaylistId),
                    body);
                if (!IsCurrentAccountPlaylistBinding(accountContext->Binding))
                {
                    co_return;
                }
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                if (!IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    co_return;
                }
                ShowLibraryDetail(
                    L"playlist",
                    winrt::hstring(accountContext->DetailKey),
                    title.empty() ? winrt::hstring(L"Playlist") : title,
                    winrt::hstring(subtitle));
                LibraryImportStatusText().Text(L"Removed from account playlist");
            }
            catch (...)
            {
                if (IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    LibraryImportStatusText().Text(L"Could not update the account playlist");
                }
            }
            co_return;
        }

        if (!track || track.CatalogId() <= 0)
        {
            co_return;
        }
        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        if (!DatabaseService().RemoveTrackFromPlaylist(m_libraryDetailKey, track.CatalogId()))
        {
            co_return;
        }

        MarkLibraryViewsDirty();
        operationLease.reset();
        ShowLibraryDetail(L"playlist", winrt::hstring(m_libraryDetailKey), title.empty() ? winrt::hstring(L"Playlist") : title, winrt::hstring(subtitle));
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryRowMenuMoveInPlaylist_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        if (m_libraryDetailKind != L"playlist" || m_libraryDetailKey.empty() || !DatabaseService().IsInitialized())
        {
            co_return;
        }
        auto track = TrackFromActionSender(sender);
        auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>();
        // The tag doubles as the marker LibraryDetailRowMenu_Opening uses to
        // hide the playlist-only actions, so the direction is read off the end
        // of it rather than out of a tag of its own.
        auto tag = item ? ReadTagString(item.Tag()) : winrt::hstring{};
        auto direction = tag == L"PlaylistTrackActionUp"
            ? std::wstring_view{ L"Up" }
            : (tag == L"PlaylistTrackActionDown"
                ? std::wstring_view{ L"Down" }
                : std::wstring_view{});
        if (!track || direction.empty())
        {
            co_return;
        }

        static constexpr wchar_t accountPrefix[] = L"account-playlist|";
        auto accountPlaylist = m_libraryDetailKey.starts_with(accountPrefix);
        auto accountContext = accountPlaylist
            ? CaptureAccountPlaylistDetailContext()
            : std::optional<AccountPlaylistDetailContext>{};
        if (accountPlaylist && !accountContext)
        {
            LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
            co_return;
        }
        auto tracks = accountPlaylist
            ? DatabaseService().LoadAccountPlaylistTracks(
                accountContext->Binding.OwnerId,
                accountContext->PlaylistId)
            : DatabaseService().LoadTracksForGroup(L"playlist", m_libraryDetailKey);
        auto found = std::find_if(tracks.begin(), tracks.end(), [&](auto const& candidate)
        {
            return accountPlaylist
                ? candidate.RemoteId() == track.RemoteId()
                : candidate.CatalogId() == track.CatalogId();
        });
        if (found == tracks.end())
        {
            co_return;
        }
        auto index = static_cast<std::size_t>(std::distance(tracks.begin(), found));
        auto target = direction == L"Up" ? (index == 0 ? index : index - 1)
            : ((index + 1 >= tracks.size()) ? index : index + 1);
        if (target == index)
        {
            co_return;
        }
        std::swap(tracks[index], tracks[target]);

        auto title = LibraryDetailTitleText() ? LibraryDetailTitleText().Text() : winrt::hstring{};
        auto subtitle = m_libraryDetailSubtitle;
        if (accountPlaylist)
        {
            if (!accountContext
                || !IsCurrentAccountPlaylistDetailContext(*accountContext))
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }
            try
            {
                auto body = BuildAccountPlaylistUpdateJson(title, tracks);
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    accountContext->Binding.Scope,
                    winrt::hstring(accountContext->PlaylistId),
                    body);
                if (!IsCurrentAccountPlaylistBinding(accountContext->Binding))
                {
                    co_return;
                }
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                if (!IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    co_return;
                }
            }
            catch (...)
            {
                if (accountContext
                    && IsCurrentAccountPlaylistDetailContext(*accountContext))
                {
                    LibraryImportStatusText().Text(L"Could not reorder the account playlist");
                }
                co_return;
            }
        }
        else
        {
            std::vector<int64_t> trackIds;
            trackIds.reserve(tracks.size());
            for (auto const& candidate : tracks)
            {
                trackIds.push_back(candidate.CatalogId());
            }
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            if (!DatabaseService().ReorderPlaylistTracks(m_libraryDetailKey, trackIds))
            {
                LibraryImportStatusText().Text(L"Could not reorder the playlist");
                co_return;
            }
            MarkLibraryViewsDirty();
            operationLease.reset();
        }

        auto refreshKey = accountPlaylist
            ? accountContext->DetailKey
            : m_libraryDetailKey;
        ShowLibraryDetail(
            L"playlist",
            winrt::hstring(refreshKey),
            title.empty() ? winrt::hstring(L"Playlist") : title,
            winrt::hstring(subtitle));
        LibraryImportStatusText().Text(L"Playlist order updated");
    }

    void MainWindow::LibraryRowMenuAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible &&
            (m_libraryDetailKind == L"album" || m_libraryDetailKind == L"album-collection"))
        {
            return;
        }

        OpenSongsTrackAlbum(TrackFromActionSender(sender));
    }


    std::vector<winrt::Last_Music_Player::TrackInfo> MainWindow::TracksForGroupCard(winrt::Last_Music_Player::TrackInfo const& group)
    {
        if (!group)
        {
            return {};
        }
        std::wstring kind{ group.SourceKind().c_str() };
        std::wstring key = group.SourceUrl().empty()
            ? std::wstring{ group.Title().c_str() }
            : std::wstring{ group.SourceUrl().c_str() };

        // Auto-playlists (Daily Mix / On Repeat / Discover Weekly) live in
        // m_homeMixes — synthesised client-side, never persisted, so the
        // DatabaseService can't resolve them.
        if (kind == L"auto-playlist")
        {
            auto it = m_homeMixes.find(key);
            if (it == m_homeMixes.end())
            {
                return {};
            }
            return it->second;
        }

        if (!DatabaseService().IsInitialized() || kind.empty() || key.empty())
        {
            return {};
        }
        return DatabaseService().LoadTracksForGroup(kind, key);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RunArtistCardActionAsync(
        winrt::Last_Music_Player::TrackInfo group,
        ArtistCardAction action)
    {
        auto lifetime = get_strong();
        auto const publicId = group ? std::wstring(group.RemoteId().c_str()) : std::wstring{};
        if (!LastMusicPlayer::Backend::IsValidLibraryArtistPublicId(publicId))
        {
            co_return;
        }
        auto& remote = detail::RemoteMusicServiceService();
        auto const scope = remote.CaptureScope();
        if (scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || !remote.IsModeAvailable(scope.Mode))
        {
            co_return;
        }
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(L"Loading saved artist tracks...");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        std::optional<std::vector<LastMusicPlayer::Backend::AccountLibraryTrackDto>> parsed;
        try
        {
            auto payload = co_await remote.GetLibraryArtistTracksAsync(
                scope,
                winrt::hstring(publicId));
            if (!remote.IsCurrent(scope))
            {
                co_return;
            }
            auto dispatcher = DispatcherQueue();
            co_await winrt::resume_background();
            parsed = LastMusicPlayer::Backend::ParseLibraryArtistTracks(payload);
            co_await wil::resume_foreground(dispatcher);
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (winrt::hresult_error const& error)
        {
            if (remote.IsCurrent(scope) && ArtistLibraryStatusText())
            {
                auto const status = LastMusicPlayer::Backend::AccountHttpStatus(error);
                ArtistLibraryStatusText().Text(status == 0
                    ? winrt::hstring{ L"Saved artist tracks are temporarily unavailable." }
                    : LastMusicPlayer::Backend::SafeAccountErrorMessage(status));
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }
        catch (...)
        {
            if (remote.IsCurrent(scope) && ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(L"Saved artist tracks are temporarily unavailable.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }

        if (!parsed || !remote.IsCurrent(scope))
        {
            if (ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(L"The saved-track response was not accepted.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        tracks.reserve(parsed->size());
        for (auto const& item : *parsed)
        {
            tracks.push_back(TrackFromLibraryArtistDto(item));
        }
        if (tracks.empty())
        {
            if (ArtistLibraryStatusText())
            {
                ArtistLibraryStatusText().Text(L"This artist has no saved tracks.");
                ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }
            co_return;
        }
        if (ArtistLibraryStatusText())
        {
            ArtistLibraryStatusText().Text(L"");
            ArtistLibraryStatusText().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        switch (action)
        {
        case ArtistCardAction::PlayNow:
            QueueAndPlayVisible(tracks, tracks.front());
            break;
        case ArtistCardAction::PlayNext:
            if (!AudioPlayerService().GetCurrentTrack())
            {
                QueueAndPlayVisible(tracks, tracks.front());
            }
            else
            {
                PlayNextFromSongsBulk(tracks);
            }
            break;
        case ArtistCardAction::AddToQueue:
            AddSongsTracksToQueueBulk(tracks);
            break;
        case ArtistCardAction::Shuffle:
        {
            std::random_device seed;
            std::mt19937 generator{ seed() };
            std::shuffle(tracks.begin(), tracks.end(), generator);
            EnsureShuffleOn();
            QueueAndPlayVisible(tracks, tracks.front());
            break;
        }
        }
    }

    void MainWindow::LibraryGroupMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto group = TrackFromActionSender(sender);
        if (group && group.SourceKind() == L"artist" && group.Provider() == L"account")
        {
            detail::RunDetached(RunArtistCardActionAsync(group, ArtistCardAction::PlayNow));
            return;
        }
        auto tracks = TracksForGroupCard(group);
        if (tracks.empty())
        {
            return;
        }
        QueueAndPlayVisible(tracks, tracks.front());
    }

    void MainWindow::LibraryGroupMenuPlayNext_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto group = TrackFromActionSender(sender);
        if (group && group.SourceKind() == L"artist" && group.Provider() == L"account")
        {
            detail::RunDetached(RunArtistCardActionAsync(group, ArtistCardAction::PlayNext));
            return;
        }
        auto tracks = TracksForGroupCard(group);
        if (tracks.empty())
        {
            return;
        }

        // Nothing playing → "Play next" behaves like "Play now" so the
        // user actually hears something. Same fallback as
        // LibraryRowMenuPlayNext_Click for the single-track case.
        if (!AudioPlayerService().GetCurrentTrack())
        {
            QueueAndPlayVisible(tracks, tracks.front());
            return;
        }
        PlayNextFromSongsBulk(tracks);
    }

    void MainWindow::LibraryGroupMenuAddToQueue_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto group = TrackFromActionSender(sender);
        if (group && group.SourceKind() == L"artist" && group.Provider() == L"account")
        {
            detail::RunDetached(RunArtistCardActionAsync(group, ArtistCardAction::AddToQueue));
            return;
        }
        auto tracks = TracksForGroupCard(group);
        if (tracks.empty())
        {
            return;
        }
        AddSongsTracksToQueueBulk(tracks);
    }

    void MainWindow::LibraryGroupMenuShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto group = TrackFromActionSender(sender);
        if (group && group.SourceKind() == L"artist" && group.Provider() == L"account")
        {
            detail::RunDetached(RunArtistCardActionAsync(group, ArtistCardAction::Shuffle));
            return;
        }
        auto tracks = TracksForGroupCard(group);
        if (tracks.empty())
        {
            return;
        }
        // Pre-shuffle the vector before queueing so:
        //   (a) the first played track is random — not natural-order #0,
        //       which QueueAndPlayVisible would otherwise pick;
        //   (b) the Up Next rail visibly reflects the random order
        //       (RebuildUpNextQueue walks the queue linearly, ignoring
        //       m_queue.ShuffleOrder, so without this the rail would
        //       look "unshuffled" and the user wouldn't see the effect).
        // EnsureShuffleOn keeps global shuffle on for subsequent advances
        // and matches expectations after they end the group.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(tracks.begin(), tracks.end(), gen);
        EnsureShuffleOn();
        QueueAndPlayVisible(tracks, tracks.front());
    }


    void MainWindow::LibraryHeaderBar_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        ApplyLibraryHeaderResponsive(args.NewSize().Width);
    }

    // Collapses the Library header action buttons to icon-only (with tooltips)
    // when the header is too narrow to show their labels, so the title and the
    // buttons stop fighting for horizontal space.
    void MainWindow::ApplyLibraryHeaderResponsive(double width)
    {
        using winrt::Microsoft::UI::Xaml::Visibility;
        bool compact = width > 0.0 && width < 780.0;

        struct Row
        {
            winrt::Microsoft::UI::Xaml::Controls::Button btn;
            winrt::Microsoft::UI::Xaml::Controls::TextBlock lbl;
            winrt::hstring tip;
        };
        Row rows[] = {
            { LibraryCreateAlbumButton(),    LblCreateAlbum(),    L"Create album" },
            { LibraryImportAlbumButton(),    LblImportAlbum(),    L"Import album" },
            { LibraryCreatePlaylistButton(), LblCreatePlaylist(), L"Create playlist" },
            { LibraryImportPlaylistButton(), LblImportPlaylist(), L"Import playlist" },
            { LibraryFollowArtistButton(),    LblFollowArtist(),    L"Follow artist" },
            { LibraryAddFolderButton(),      LblAddFolder(),      L"Add folder" },
        };

        for (auto const& r : rows)
        {
            if (r.lbl)
            {
                r.lbl.Visibility(compact ? Visibility::Collapsed : Visibility::Visible);
            }
            if (r.btn)
            {
                winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                    r.btn,
                    compact ? winrt::box_value(r.tip)
                            : winrt::Windows::Foundation::IInspectable{ nullptr });
            }
        }
    }

}
