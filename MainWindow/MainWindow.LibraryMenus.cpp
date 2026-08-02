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
        auto albumsActive = isChecked(LibTabAlbums());
        auto playlistsActive = isChecked(LibTabPlaylists());
        auto manualPlaylistsActive = playlistsActive && m_libraryPlaylistFilter == L"Manual";
        auto accountPlaylistScope = manualPlaylistsActive && m_libraryScope == L"Account";
        auto accountMode = RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account;
        auto accountPlaylistWritable = accountMode
            && AccountSessionService().Status() == LastMusicPlayer::Backend::AccountSessionStatus::Validated;

        if (LibraryCreateAlbumButton())
        {
            LibraryCreateAlbumButton().Visibility(!detailVisible && albumsActive ? V::Visible : V::Collapsed);
        }
        if (LibraryImportAlbumButton())
        {
            LibraryImportAlbumButton().Visibility(!detailVisible && albumsActive && !accountMode ? V::Visible : V::Collapsed);
        }
        if (LibraryCreatePlaylistButton())
        {
            LibraryCreatePlaylistButton().Visibility(!detailVisible && manualPlaylistsActive ? V::Visible : V::Collapsed);
            LibraryCreatePlaylistButton().IsEnabled(!accountPlaylistScope || accountPlaylistWritable);
        }
        if (LibraryImportPlaylistButton())
        {
            LibraryImportPlaylistButton().Visibility(!detailVisible && manualPlaylistsActive ? V::Visible : V::Collapsed);
            LibraryImportPlaylistButton().IsEnabled(!accountMode || (accountPlaylistScope && accountPlaylistWritable));
        }
        if (LibraryAddFolderButton())
        {
            LibraryAddFolderButton().Visibility(detailVisible ? V::Collapsed : V::Visible);
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
            std::vector<winrt::hstring> remoteIds;
            remoteIds.reserve(tracks.size());
            for (auto const& item : tracks)
            {
                if (!item.RemoteId().empty() && item.RemoteId() != track.RemoteId())
                {
                    remoteIds.push_back(item.RemoteId());
                }
            }
            try
            {
                auto body = BuildAccountPlaylistUpdateJson(title, remoteIds);
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
                co_await SynchronizeAccountLibraryAsync(false);
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
        auto direction = item ? ReadTagString(item.Tag()) : winrt::hstring{};
        if (!track || (direction != L"Up" && direction != L"Down"))
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
            std::vector<winrt::hstring> remoteIds;
            remoteIds.reserve(tracks.size());
            for (auto const& candidate : tracks)
            {
                remoteIds.push_back(candidate.RemoteId());
            }
            try
            {
                auto body = BuildAccountPlaylistUpdateJson(title, remoteIds);
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
                co_await SynchronizeAccountLibraryAsync(false);
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

    void MainWindow::LibraryGroupMenuPlayNow_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto group = TrackFromActionSender(sender);
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
