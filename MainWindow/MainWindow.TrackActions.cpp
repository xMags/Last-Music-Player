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
    std::wstring AccountEventTimestampNow()
    {
        SYSTEMTIME now{};
        ::GetSystemTime(&now);
        wchar_t value[40]{};
        swprintf_s(value, L"%04u-%02u-%02uT%02u:%02u:%02uZ",
            now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
        return value;
    }
}

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    winrt::Last_Music_Player::TrackInfo MainWindow::TrackFromActionSender(winrt::Windows::Foundation::IInspectable const& sender)
    {
        if (auto item = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem>())
        {
            if (auto track = item.CommandParameter().try_as<winrt::Last_Music_Player::TrackInfo>())
            {
                return track;
            }
            if (auto track = item.Tag().try_as<winrt::Last_Music_Player::TrackInfo>())
            {
                return track;
            }
        }

        if (auto element = sender.try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            if (auto track = element.Tag().try_as<winrt::Last_Music_Player::TrackInfo>())
            {
                return track;
            }
            if (auto track = element.DataContext().try_as<winrt::Last_Music_Player::TrackInfo>())
            {
                return track;
            }
        }

        return nullptr;
    }


    int64_t MainWindow::PersistTrackForPlaylist(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track || !DatabaseService().IsInitialized())
        {
            return 0;
        }

        if (RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && IsCompatibleAccountRemoteTrack(track))
        {
            return 0;
        }
        if (track.CatalogId() > 0)
        {
            return track.CatalogId();
        }


        auto key = CatalogSourceKey(track);
        if (key.empty())
        {
            return 0;
        }

        auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
        return remote
            ? DatabaseService().UpsertRemoteTrack(track, key)
            : DatabaseService().UpsertLocalTrack(track, key);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AddTrackToPlaylistAsync(winrt::Last_Music_Player::TrackInfo track)
    {
        if (!track || !IsPlayableHomeTrack(track) || !DatabaseService().IsInitialized())
        {
            co_return;
        }
        auto operationGeneration = UserDataOperationGateService().Generation();

        if (m_manualPlaylists.Size() == 0)
        {
            co_await HydrateLibraryTabAsync(L"Playlists", false);
        }
        if (!UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            co_return;
        }

        winrt::Microsoft::UI::Xaml::Controls::StackPanel content;
        content.Spacing(12);
        auto accountTrack = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && IsCompatibleAccountRemoteTrack(track);
        auto accountOnlyScope = accountTrack;

        winrt::Microsoft::UI::Xaml::Controls::ComboBox playlistPicker;
        playlistPicker.Header(winrt::box_value(winrt::hstring(L"Playlist")));
        playlistPicker.PlaceholderText(L"Choose a playlist");
        for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
        {
            auto playlist = m_manualPlaylists.GetAt(i);
            auto accountPlaylist = playlist.Provider() == L"account";
            if ((accountOnlyScope && !accountPlaylist) || (!accountOnlyScope && accountPlaylist))
            {
                continue;
            }
            winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem item;
            item.Content(winrt::box_value(playlist.Title()));
            item.Tag(winrt::box_value(playlist.SourceUrl()));
            playlistPicker.Items().Append(item);
        }
        if (playlistPicker.Items().Size() > 0)
        {
            playlistPicker.SelectedIndex(0);
        }
        content.Children().Append(playlistPicker);

        winrt::Microsoft::UI::Xaml::Controls::TextBox newNameBox;
        newNameBox.Header(winrt::box_value(winrt::hstring(L"Or create new")));
        newNameBox.PlaceholderText(L"New playlist name");
        if (!accountOnlyScope)
        {
            content.Children().Append(newNameBox);
        }

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Add to playlist")));
        dlg.Content(content);
        dlg.PrimaryButtonText(L"Add");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }
        if (!UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            LibraryImportStatusText().Text(L"Cleanup interrupted the playlist change");
            co_return;
        }

        std::wstring playlistKey;
        winrt::Last_Music_Player::TrackInfo selectedPlaylist{ nullptr };
        auto newName = TrimQuery(newNameBox.Text());
        if (!newName.empty())
        {
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease
                || !UserDataOperationGateService().IsCurrent(operationGeneration))
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            auto playlistId = DatabaseService().CreatePlaylist(std::wstring(newName.c_str()));
            operationLease.reset();
            co_await HydrateLibraryTabAsync(L"Playlists", true);
            if (!UserDataOperationGateService().IsCurrent(operationGeneration))
            {
                co_return;
            }
            for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
            {
                auto playlist = m_manualPlaylists.GetAt(i);
                if (playlist.CatalogId() == playlistId)
                {
                    playlistKey = std::wstring(playlist.SourceUrl().c_str());
                    break;
                }
            }
        }
        else if (auto selected = playlistPicker.SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem>())
        {
            playlistKey = std::wstring(winrt::unbox_value_or<winrt::hstring>(selected.Tag(), L"").c_str());
            for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
            {
                auto playlist = m_manualPlaylists.GetAt(i);
                if (playlist.SourceUrl() == winrt::hstring(playlistKey))
                {
                    selectedPlaylist = playlist;
                    break;
                }
            }
        }

        if (accountOnlyScope && !selectedPlaylist)
        {
            LibraryImportStatusText().Text(L"Create an account playlist before adding songs");
            co_return;
        }

        if (playlistKey.empty())
        {
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease
                || !UserDataOperationGateService().IsCurrent(operationGeneration))
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            auto playlistId = DatabaseService().CreatePlaylist(L"New Playlist");
            operationLease.reset();
            co_await HydrateLibraryTabAsync(L"Playlists", true);
            if (!UserDataOperationGateService().IsCurrent(operationGeneration))
            {
                co_return;
            }
            for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
            {
                auto playlist = m_manualPlaylists.GetAt(i);
                if (playlist.CatalogId() == playlistId)
                {
                    playlistKey = std::wstring(playlist.SourceUrl().c_str());
                    break;
                }
            }
        }
        if (selectedPlaylist && selectedPlaylist.Provider() == L"account")
        {
            auto accountBinding = AccountPlaylistBindingFor(selectedPlaylist);
            if (!accountBinding
                || !IsCurrentAccountPlaylistBinding(*accountBinding)
                || selectedPlaylist.RemoteId().empty())
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }
            auto trackJson = BuildAccountTrackJson(track);
            if (trackJson.empty())
            {
                LibraryImportStatusText().Text(L"Only compatible account tracks can be added to an account playlist");
                co_return;
            }
            try
            {
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease
                    || !UserDataOperationGateService().IsCurrent(operationGeneration))
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().AddAccountPlaylistTrackAsync(
                    accountBinding->Scope,
                    selectedPlaylist.RemoteId(),
                    trackJson);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                LibraryImportStatusText().Text(L"Added to account playlist");
            }
            catch (...)
            {
                LibraryImportStatusText().Text(L"Could not update the account playlist");
            }
            co_return;
        }


        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease
            || !UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto trackId = PersistTrackForPlaylist(track);
        if (playlistKey.empty() || trackId <= 0 || !DatabaseService().AddTrackToPlaylist(playlistKey, trackId))
        {
            LibraryImportStatusText().Text(L"Could not add to playlist");
            co_return;
        }

        auto detailTitle = LibraryDetailTitleText() ? LibraryDetailTitleText().Text() : winrt::hstring{};
        auto detailSubtitle = m_libraryDetailSubtitle;
        auto inSamePlaylist = m_libraryDetailKind == L"playlist" && m_libraryDetailKey == playlistKey;
        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Playlists", true);
        if (inSamePlaylist)
        {
            ShowLibraryDetail(L"playlist", winrt::hstring(playlistKey), detailTitle.empty() ? winrt::hstring(L"Playlist") : detailTitle, winrt::hstring(detailSubtitle));
        }
        LibraryImportStatusText().Text(L"Added to playlist");
    }

    void MainWindow::ToggleTrackLiked(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return;
        }

        auto accountTrack = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && ToLowerCopy(track.SourceKind()) == L"remote"
            && !track.RemoteId().empty();
        auto key = accountTrack
            ? (L"remote-id|" + std::wstring(track.RemoteId().c_str()))
            : CatalogSourceKey(track);
        if (key.empty())
        {
            return;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            return;
        }

        bool liked = !track.IsLiked();
        if (DatabaseService().IsInitialized())
        {
            if (accountTrack)
            {
                try
                {
                    auto context = RemoteMusicServiceService().CaptureAccountSyncContext();
                    auto ownerId = std::wstring(context.OwnerId().c_str());
                    auto remoteId = std::wstring(track.RemoteId().c_str());
                    LastMusicPlayer::Backend::PendingLikeRecord pending;
                    pending.AccountId = ownerId;
                    pending.RemoteTrackId = remoteId;
                    pending.Track = track;
                    pending.DesiredState = liked;
                    pending.UpdatedAtUtc = AccountEventTimestampNow();
                    if (!RemoteMusicServiceService().IsCurrent(context)
                        || !DatabaseService().EnsureAccountTrack(ownerId, track)
                        || !DatabaseService().UpdateAccountTrackLike(ownerId, remoteId, liked))
                    {
                        return;
                    }
                    if (!DatabaseService().EnqueuePendingLike(pending))
                    {
                        DatabaseService().UpdateAccountTrackLike(ownerId, remoteId, !liked);
                        return;
                    }
                    if (!RemoteMusicServiceService().IsCurrent(context))
                    {
                        return;
                    }
                }
                catch (...)
                {
                    return;
                }
            }
            else
            {
                auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
                if (remote)
                {
                    DatabaseService().UpsertRemoteTrack(track, key);
                }
                else
                {
                    DatabaseService().UpsertLocalTrack(track, key);
                }

                liked = !DatabaseService().IsLiked(key);
                DatabaseService().SetLiked(key, liked);
            }
        }
        operationLease.reset();

        auto matchesTrack = [&](winrt::Last_Music_Player::TrackInfo const& candidate)
        {
            if (!candidate)
            {
                return false;
            }
            return accountTrack
                ? candidate.RemoteId() == track.RemoteId()
                : CatalogSourceKey(candidate) == key;
        };
        auto patchLike = [&](winrt::Last_Music_Player::TrackInfo const& candidate)
        {
            if (matchesTrack(candidate))
            {
                candidate.IsLiked(liked);
            }
        };

        patchLike(track);
        if (auto current = AudioPlayerService().GetCurrentTrack())
        {
            patchLike(current);
            if (matchesTrack(current))
            {
                UpdateLikeButton(current);
            }
        }

        auto patchObservable = [&](winrt::Windows::Foundation::Collections::IObservableVector<winrt::Last_Music_Player::TrackInfo> const& items)
        {
            for (uint32_t i = 0; i < items.Size(); ++i)
            {
                patchLike(items.GetAt(i));
            }
        };

        for (auto& item : m_queue.CurrentPlaylist) { patchLike(item); }
        for (auto& item : m_queue.Queue) { patchLike(item); }
        for (auto& item : m_homeRecentHistory) { patchLike(item); }
        for (auto& item : m_catalogTracks) { patchLike(item); }
        for (auto& item : m_searchAllResults) { patchLike(item); }
        for (auto& item : m_songsAllResults) { patchLike(item); }
        for (auto& item : m_librarySongAllResults) { patchLike(item); }
        for (auto& item : m_libraryDetailAllResults) { patchLike(item); }
        for (auto& mix : m_homeMixes)
        {
            for (auto& item : mix.second)
            {
                patchLike(item);
            }
        }
        patchObservable(m_songsTracks);
        patchObservable(m_homeTracks);
        patchObservable(m_recentlyAddedTracks);
        patchObservable(m_searchTracks);
        patchObservable(m_librarySongs);
        patchObservable(m_libraryDetailTracks);
        patchObservable(m_upNextQueue);
        patchObservable(m_discoverChartItems);
        patchObservable(m_discoverDetailTracks);

        if (accountTrack)
        {
            m_catalogLikeOverrides.insert_or_assign(std::wstring{ track.RemoteId().c_str() }, liked);
            if (m_discoverLoaded)
            {
                RebuildCatalogSurfaces();
            }
        }

        auto detailKind = m_libraryDetailKind;
        auto detailKey = m_libraryDetailKey;
        auto detailTitle = LibraryDetailTitleText() ? LibraryDetailTitleText().Text() : winrt::hstring{};
        auto detailSubtitle = m_libraryDetailSubtitle;

        MarkLibraryViewsDirty();
        RunDetached(HydrateHomeAsync(false));
        if (accountTrack)
        {
            RunDetached(SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit));
        }

        if (!detailKind.empty() && !detailKey.empty())
        {
            ShowLibraryDetail(winrt::hstring(detailKind), winrt::hstring(detailKey), detailTitle.empty() ? winrt::hstring(detailKey) : detailTitle, winrt::hstring(detailSubtitle));
        }

    }

    void MainWindow::OpenSongsTrackArtist(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return;
        }

        auto artist = track.Artist().empty() ? winrt::hstring{ L"Unknown Artist" } : track.Artist();
        ShowPrimaryView(L"Library");
        ShowLibraryDetail(L"artist", artist, artist, L"");
    }

    void MainWindow::OpenSongsTrackAlbum(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track)
        {
            return;
        }

        auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
        auto album = track.Album().empty()
            ? (remote ? winrt::hstring{ L"Remote Singles" } : winrt::hstring{ L"Unknown Album" })
            : track.Album();

        ShowPrimaryView(L"Library");
        ShowLibraryDetail(L"album", album, album, track.Artist(), ApprovedDetailArtwork(track, L"album"), track);
    }


    winrt::Windows::Foundation::IAsyncAction MainWindow::LikedSongsButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        // Opens the Favourites system playlist, the same destination as Home's
        // "See all Favourites". This used to filter the Songs tab instead, but
        // that filter is gone: one liked-songs view is enough.
        co_await OpenLibrarySystemPlaylistAsync(L"smart-liked");
    }

    void MainWindow::LikeCurrentTrack_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto current = AudioPlayerService().GetCurrentTrack();
        ToggleTrackLiked(current);
    }

}
