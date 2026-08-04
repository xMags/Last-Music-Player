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

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryCreateAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::Microsoft::UI::Xaml::Controls::TextBox nameBox;
        nameBox.Header(winrt::box_value(winrt::hstring(L"Album name")));
        nameBox.PlaceholderText(L"New album");
        nameBox.Width(320);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Create album")));
        dlg.Content(nameBox);
        dlg.PrimaryButtonText(L"Create");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto title = nameBox.Text();
        if (TrimQuery(title).empty())
        {
            title = L"Untitled Album";
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }

        auto albumId = DatabaseService().CreateAlbumCollection(std::wstring(title.c_str()));
        (void)albumId;
        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Albums", true);
        LibraryImportStatusText().Text(L"Album created");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryImportAlbum_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::Microsoft::UI::Xaml::Controls::TextBox urlBox;
        urlBox.Header(winrt::box_value(winrt::hstring(L"Music API album link")));
        urlBox.PlaceholderText(L"Paste album link");
        // Pin the input width so a long URL doesn't blow the dialog out
        // horizontally (the default ContentDialog auto-sizes to content
        // and the TextBox grows to fit a single-line paste).
        urlBox.Width(320);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Import album")));
        dlg.Content(urlBox);
        dlg.PrimaryButtonText(L"Import");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto url = TrimQuery(urlBox.Text());
        if (url.empty())
        {
            co_return;
        }
        if (RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            LibraryImportStatusText().Text(L"Account mode supports playlist imports from the Playlists view");
            co_return;
        }


        try
        {
            LibraryImportStatusText().Text(L"Importing album...");

            auto& remoteMusic = RemoteMusicServiceService();
            auto remoteScope = remoteMusic.CaptureScope();
            auto payload = co_await remoteMusic.ImportAsync(remoteScope, url);
            auto root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
            auto albumObject = root.GetNamedObject(L"album");
            auto trackArray = root.GetNamedArray(L"tracks");
            auto tracks = ParseProviderTrackArray(trackArray);
            if (tracks.empty())
            {
                LibraryImportStatusText().Text(L"No tracks found in that album");
                co_return;
            }

            auto provider = albumObject.GetNamedString(L"provider", L"remote");
            auto sourceUrl = albumObject.GetNamedString(L"sourceUrl", url);
            auto albumTitle = albumObject.GetNamedString(L"title", L"Imported Album");
            auto albumArtist = albumObject.GetNamedString(L"artist", L"Music API");
            auto artworkUrl = albumObject.GetNamedString(L"artworkUrl", L"");
            auto sourceLabel = albumObject.GetNamedString(L"sourceLabel", L"Music API");
            auto collectionKind = ToLowerCopy(albumObject.GetNamedString(L"collectionKind", L"album"));
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }

            if (collectionKind == L"playlist")
            {
                sourceUrl = CanonicalProviderCollectionSourceUrl(sourceUrl);
                auto playlistKey = L"import-playlist|" + ToLowerCopy(provider) + L"|" + std::wstring(sourceUrl.c_str());

                LastMusicPlayer::Backend::TrackInfo playlist;
                playlist.Title(albumTitle);
                playlist.Artist(albumArtist);
                playlist.Provider(provider);
                playlist.SourceKind(L"manual");
                playlist.SourceUrl(sourceUrl);
                playlist.SourceLabel(sourceLabel);
                ApplyMusicArtwork(playlist, artworkUrl, L"playlist");

                auto playlistId = DatabaseService().UpsertPlaylist(playlist, playlistKey);
                std::vector<int64_t> trackIds;
                trackIds.reserve(tracks.size());
                for (auto const& track : tracks)
                {
                    auto key = CatalogSourceKey(track);
                    if (key.empty())
                    {
                        continue;
                    }

                    auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
                    auto trackId = remote
                        ? DatabaseService().UpsertRemoteTrack(track, key)
                        : DatabaseService().UpsertLocalTrack(track, key);
                    if (trackId > 0)
                    {
                        trackIds.push_back(trackId);
                    }
                }

                if (playlistId <= 0 || trackIds.empty())
                {
                    LibraryImportStatusText().Text(L"Import failed: no playable tracks were saved");
                    co_return;
                }

                DatabaseService().ReplacePlaylistTracks(playlistId, trackIds);
                MarkLibraryViewsDirty();
                operationLease.reset();
                if (LibTabPlaylists())
                {
                    LibTabPlaylists().IsChecked(true);
                    LibraryTab_Checked(LibTabPlaylists(), nullptr);
                }
                co_await HydrateLibraryTabAsync(L"Playlists", true);
                LibraryImportStatusText().Text(winrt::hstring(std::wstring(L"Imported playlist with ") + std::to_wstring(trackIds.size()) + L" songs"));

                for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
                {
                    auto imported = m_manualPlaylists.GetAt(i);
                    if (imported.CatalogId() == playlistId || imported.SourceUrl() == winrt::hstring(playlistKey))
                    {
                        ShowLibraryDetail(L"playlist", imported.SourceUrl(), imported.Title(), imported.Artist(), ApprovedDetailArtwork(imported, L"playlist"));
                        break;
                    }
                }
                co_return;
            }
            auto albumKey = L"import|" + ToLowerCopy(provider) + L"|" + std::wstring(sourceUrl.c_str());

            LastMusicPlayer::Backend::TrackInfo album;
            album.Title(albumTitle);
            album.Artist(albumArtist);
            album.Provider(provider);
            album.SourceKind(provider);
            album.SourceUrl(sourceUrl);
            album.SourceLabel(sourceLabel);
            ApplyMusicArtwork(album, artworkUrl, L"album-collection");

            auto albumId = DatabaseService().UpsertAlbumCollection(album, albumKey);
            std::vector<int64_t> trackIds;
            trackIds.reserve(tracks.size());
            for (auto const& track : tracks)
            {
                auto key = CatalogSourceKey(track);
                if (key.empty())
                {
                    continue;
                }

                auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
                auto trackId = remote
                    ? DatabaseService().UpsertRemoteTrack(track, key)
                    : DatabaseService().UpsertLocalTrack(track, key);
                if (trackId > 0)
                {
                    trackIds.push_back(trackId);
                }
            }

            if (albumId <= 0 || trackIds.empty())
            {
                LibraryImportStatusText().Text(L"Import failed: no playable tracks were saved");
                co_return;
            }

            DatabaseService().ReplaceAlbumCollectionTracks(albumId, trackIds);
            MarkLibraryViewsDirty();
            operationLease.reset();
            co_await HydrateLibraryTabAsync(L"Albums", true);
            LibraryImportStatusText().Text(winrt::hstring(std::wstring(L"Imported ") + std::to_wstring(trackIds.size()) + L" songs"));
            winrt::Microsoft::UI::Xaml::Media::ImageSource detailArt{ nullptr };
            for (uint32_t i = 0; i < m_albums.Size(); ++i)
            {
                auto importedAlbum = m_albums.GetAt(i);
                if (importedAlbum.SourceUrl() == winrt::hstring(albumKey))
                {
                    detailArt = ApprovedDetailArtwork(importedAlbum, L"album-collection");
                    break;
                }
            }
            ShowLibraryDetail(L"album-collection", winrt::hstring(albumKey), albumTitle, albumArtist, detailArt);
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (winrt::hresult_error const& ex)
        {
            LibraryImportStatusText().Text(winrt::hstring(L"Import failed: ") + ex.message());
        }
        catch (...)
        {
            LibraryImportStatusText().Text(L"Import failed");
        }
    }


    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryCreatePlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!DatabaseService().IsInitialized())
        {
            co_return;
        }
        auto remoteScope = RemoteMusicServiceService().CaptureScope();

        winrt::Microsoft::UI::Xaml::Controls::TextBox nameBox;
        nameBox.Header(winrt::box_value(winrt::hstring(L"Playlist name")));
        nameBox.PlaceholderText(L"Road trip, night drive, work focus...");
        nameBox.Width(320);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Create playlist")));
        dlg.Content(nameBox);
        dlg.PrimaryButtonText(L"Create");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto title = TrimQuery(nameBox.Text());
        if (title.empty())
        {
            title = L"Untitled Playlist";
        }
        if (m_libraryScope == L"Account")
        {
            if (remoteScope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
                || !RemoteMusicServiceService().IsCurrent(remoteScope)
                || AccountSessionService().Status() != LastMusicPlayer::Backend::AccountSessionStatus::Validated)
            {
                LibraryImportStatusText().Text(L"Account playlists require an online signed-in account");
                co_return;
            }
            try
            {
                auto body = BuildAccountPlaylistJson(title);
                if (body.empty())
                {
                    LibraryImportStatusText().Text(L"Playlist name is invalid");
                    co_return;
                }
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().CreateAccountPlaylistAsync(remoteScope, body);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                LibraryImportStatusText().Text(L"Account playlist created");
            }
            catch (...)
            {
                LibraryImportStatusText().Text(L"Account playlist could not be created");
            }
            co_return;
        }


        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto playlistId = DatabaseService().CreatePlaylist(std::wstring(title.c_str()));
        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Playlists", true);
        LibraryImportStatusText().Text(L"Playlist created");

        for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
        {
            auto playlist = m_manualPlaylists.GetAt(i);
            if (playlist.CatalogId() == playlistId)
            {
                ShowLibraryDetail(playlist.SourceKind(), playlist.SourceUrl(), playlist.Title(), playlist.Artist(), ApprovedDetailArtwork(playlist, L"playlist"));
                break;
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryImportPlaylist_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!DatabaseService().IsInitialized())
        {
            co_return;
        }

        winrt::Microsoft::UI::Xaml::Controls::TextBox urlBox;
        urlBox.Header(winrt::box_value(winrt::hstring(L"Music API playlist link")));
        urlBox.PlaceholderText(L"Paste playlist link");
        urlBox.Width(320);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Import playlist")));
        dlg.Content(urlBox);
        dlg.PrimaryButtonText(L"Import");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto url = CanonicalProviderCollectionSourceUrl(urlBox.Text());
        if (url.empty())
        {
            co_return;
        }
        auto& remoteMusic = RemoteMusicServiceService();
        auto remoteScope = remoteMusic.CaptureScope();
        auto accountMode = remoteScope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        if (accountMode && m_libraryScope != L"Account")
        {
            LibraryImportStatusText().Text(L"Select the Account scope before importing an account playlist");
            co_return;
        }


        try
        {
            LibraryImportStatusText().Text(accountMode ? L"Importing account playlist..." : L"Importing playlist...");
            if (accountMode)
            {
                if (AccountSessionService().Status() != LastMusicPlayer::Backend::AccountSessionStatus::Validated)
                {
                    LibraryImportStatusText().Text(L"Account playlists can only be imported while online");
                    co_return;
                }
                auto body = BuildAccountPlaylistImportJson(url);
                if (body.empty())
                {
                    LibraryImportStatusText().Text(L"That playlist link is not supported");
                    co_return;
                }
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await remoteMusic.CreateAccountPlaylistAsync(remoteScope, body);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                LibraryImportStatusText().Text(L"Account playlist imported");
                co_return;
            }

            auto payload = co_await remoteMusic.ImportAsync(remoteScope, url);

            auto root = winrt::Windows::Data::Json::JsonObject::Parse(payload);
            auto albumObject = root.GetNamedObject(L"album");
            auto trackArray = root.GetNamedArray(L"tracks");
            auto tracks = ParseProviderTrackArray(trackArray);
            if (tracks.empty())
            {
                LibraryImportStatusText().Text(L"No tracks found in that playlist");
                co_return;
            }

            auto provider = albumObject.GetNamedString(L"provider", L"remote");
            auto sourceUrl = CanonicalProviderCollectionSourceUrl(albumObject.GetNamedString(L"sourceUrl", url));
            auto playlistTitle = albumObject.GetNamedString(L"title", L"Imported Playlist");
            auto playlistCaption = albumObject.GetNamedString(L"artist", L"Music API");
            auto artworkUrl = albumObject.GetNamedString(L"artworkUrl", L"");
            auto sourceLabel = albumObject.GetNamedString(L"sourceLabel", L"Music API");
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease)
            {
                LibraryImportStatusText().Text(L"Cleanup is in progress");
                co_return;
            }
            if (!remoteMusic.IsCurrent(remoteScope))
            {
                co_return;
            }
            auto playlistKey = L"import-playlist|" + ToLowerCopy(provider) + L"|" + std::wstring(sourceUrl.c_str());

            LastMusicPlayer::Backend::TrackInfo playlist;
            playlist.Title(playlistTitle);
            playlist.Artist(playlistCaption);
            playlist.Provider(provider);
            playlist.SourceKind(L"manual");
            playlist.SourceUrl(sourceUrl);
            playlist.SourceLabel(sourceLabel);
            ApplyMusicArtwork(playlist, artworkUrl, L"playlist");

            auto playlistId = DatabaseService().UpsertPlaylist(playlist, playlistKey);
            std::vector<int64_t> trackIds;
            trackIds.reserve(tracks.size());
            for (auto const& track : tracks)
            {
                auto key = CatalogSourceKey(track);
                if (key.empty())
                {
                    continue;
                }

                auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
                auto trackId = remote
                    ? DatabaseService().UpsertRemoteTrack(track, key)
                    : DatabaseService().UpsertLocalTrack(track, key);
                if (trackId > 0)
                {
                    trackIds.push_back(trackId);
                }
            }

            if (playlistId <= 0 || trackIds.empty())
            {
                LibraryImportStatusText().Text(L"Import failed: no playable tracks were saved");
                co_return;
            }

            DatabaseService().ReplacePlaylistTracks(playlistId, trackIds);
            MarkLibraryViewsDirty();
            operationLease.reset();
            co_await HydrateLibraryTabAsync(L"Playlists", true);
            LibraryImportStatusText().Text(winrt::hstring(std::wstring(L"Imported playlist with ") + std::to_wstring(trackIds.size()) + L" songs"));

            for (uint32_t i = 0; i < m_manualPlaylists.Size(); ++i)
            {
                auto imported = m_manualPlaylists.GetAt(i);
                if (imported.CatalogId() == playlistId || imported.SourceUrl() == winrt::hstring(playlistKey))
                {
                    ShowLibraryDetail(L"playlist", imported.SourceUrl(), imported.Title(), imported.Artist(), ApprovedDetailArtwork(imported, L"playlist"));
                    break;
                }
            }
        }
        catch (winrt::hresult_canceled const&)
        {
            co_return;
        }
        catch (winrt::hresult_error const& ex)
        {
            LibraryImportStatusText().Text(winrt::hstring(L"Import failed: ") + ex.message());
        }
        catch (...)
        {
            LibraryImportStatusText().Text(L"Import failed");
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryPlaylistRename_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto playlist = TrackFromActionSender(sender);
        if (!playlist || !DatabaseService().IsInitialized() || playlist.SourceUrl().empty())
        {
            co_return;
        }
        auto accountBinding = playlist.Provider() == L"account"
            ? AccountPlaylistBindingFor(playlist)
            : std::optional<AccountPlaylistBinding>{};

        winrt::Microsoft::UI::Xaml::Controls::TextBox nameBox;
        nameBox.Header(winrt::box_value(winrt::hstring(L"Playlist name")));
        nameBox.Text(playlist.Title());
        nameBox.Width(320);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Rename playlist")));
        dlg.Content(nameBox);
        dlg.PrimaryButtonText(L"Rename");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Primary);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto title = TrimQuery(nameBox.Text());
        if (title.empty())
        {
            title = L"Untitled Playlist";
        }

        if (playlist.Provider() == L"account")
        {
            if (!accountBinding
                || !IsCurrentAccountPlaylistBinding(*accountBinding)
                || playlist.RemoteId().empty())
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }
            try
            {
                auto body = BuildAccountPlaylistJson(title);
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().UpdateAccountPlaylistAsync(
                    accountBinding->Scope,
                    playlist.RemoteId(),
                    body);
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                LibraryImportStatusText().Text(L"Account playlist renamed");
            }
            catch (...)
            {
                LibraryImportStatusText().Text(L"Account playlist rename failed");
            }
            co_return;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        if (!DatabaseService().RenamePlaylist(std::wstring(playlist.SourceUrl().c_str()), std::wstring(title.c_str())))
        {
            LibraryImportStatusText().Text(L"Playlist rename failed");
            co_return;
        }

        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Playlists", true);
        LibraryImportStatusText().Text(L"Playlist renamed");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryPlaylistDelete_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto playlist = TrackFromActionSender(sender);
        if (!playlist || !DatabaseService().IsInitialized() || playlist.SourceUrl().empty())
        {
            co_return;
        }
        auto accountBinding = playlist.Provider() == L"account"
            ? AccountPlaylistBindingFor(playlist)
            : std::optional<AccountPlaylistBinding>{};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock message;
        message.Text(L"This removes the playlist. Songs stay in your library.");
        message.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Delete playlist?")));
        dlg.Content(message);
        dlg.PrimaryButtonText(L"Delete");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        if (playlist.Provider() == L"account")
        {
            if (!accountBinding
                || !IsCurrentAccountPlaylistBinding(*accountBinding)
                || playlist.RemoteId().empty())
            {
                LibraryImportStatusText().Text(L"The account playlist changed. Refresh and try again.");
                co_return;
            }
            try
            {
                auto operationLease = UserDataOperationGateService().TryEnter();
                if (!operationLease)
                {
                    LibraryImportStatusText().Text(L"Cleanup is in progress");
                    co_return;
                }
                co_await RemoteMusicServiceService().DeleteAccountPlaylistAsync(
                    accountBinding->Scope,
                    playlist.RemoteId());
                if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible
                    && m_libraryDetailKind == L"playlist"
                    && m_libraryDetailKey == std::wstring(playlist.SourceUrl().c_str()))
                {
                    HideLibraryDetail();
                }
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);
                operationLease.reset();
                LibraryImportStatusText().Text(L"Account playlist deleted");
            }
            catch (...)
            {
                LibraryImportStatusText().Text(L"Account playlist delete failed");
            }
            co_return;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto playlistKey = std::wstring(playlist.SourceUrl().c_str());
        if (!DatabaseService().DeletePlaylist(playlistKey))
        {
            LibraryImportStatusText().Text(L"Playlist delete failed");
            co_return;
        }

        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible &&
            m_libraryDetailKind == L"playlist" &&
            m_libraryDetailKey == playlistKey)
        {
            HideLibraryDetail();
        }

        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Playlists", true);
        LibraryImportStatusText().Text(L"Playlist deleted");
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryAlbumDelete_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto album = TrackFromActionSender(sender);
        if (!album || !DatabaseService().IsInitialized())
        {
            co_return;
        }

        if (album.SourceKind() != L"album-collection" || album.SourceUrl().empty())
        {
            LibraryImportStatusText().Text(L"Only created or imported albums can be deleted");
            co_return;
        }

        winrt::Microsoft::UI::Xaml::Controls::TextBlock message;
        message.Text(album.Provider() == L"manual"
            ? L"This removes the album collection. Songs stay in your library."
            : L"This removes the imported album and its remote tracks from your library.");
        message.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Delete album?")));
        dlg.Content(message);
        dlg.PrimaryButtonText(L"Delete");
        dlg.CloseButtonText(L"Cancel");
        dlg.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        dlg.XamlRoot(this->Content().XamlRoot());

        auto result = co_await dlg.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease)
        {
            LibraryImportStatusText().Text(L"Cleanup is in progress");
            co_return;
        }
        auto albumKey = std::wstring(album.SourceUrl().c_str());
        if (!DatabaseService().DeleteAlbumCollection(albumKey))
        {
            LibraryImportStatusText().Text(L"Album delete failed");
            co_return;
        }

        if (LibraryDetailContent().Visibility() == winrt::Microsoft::UI::Xaml::Visibility::Visible &&
            m_libraryDetailKind == L"album-collection" &&
            m_libraryDetailKey == albumKey)
        {
            HideLibraryDetail();
        }

        MarkLibraryViewsDirty();
        operationLease.reset();
        co_await HydrateLibraryTabAsync(L"Albums", true);
        LibraryImportStatusText().Text(L"Album deleted");
    }

}
