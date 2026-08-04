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

    winrt::Windows::Foundation::IAsyncAction MainWindow::HydrateLibraryDetailTracksAsync()
    {
        auto lifetime = get_strong();
        if (m_libraryDetailPageLoading || m_libraryDetailKind.empty() || m_libraryDetailKey.empty())
        {
            co_return;
        }
        if (m_libraryDetailMatchedCount > 0 && static_cast<int>(m_libraryDetailAllResults.size()) >= m_libraryDetailMatchedCount)
        {
            co_return;
        }

        std::optional<AccountPlaylistDetailContext> accountContext;
        if (IsAccountPlaylistDetail())
        {
            accountContext = CaptureAccountPlaylistDetailContext();
            if (!accountContext)
            {
                HideLibraryDetail();
                co_return;
            }
        }

        auto dispatcher = this->DispatcherQueue();
        auto epoch = m_libraryDetailHydrationEpoch;
        auto offset = static_cast<uint32_t>(m_libraryDetailAllResults.size());
        auto query = CurrentLibraryDetailQuery(
            offset,
            kLibrarySongPageSize,
            accountContext ? accountContext->Binding.OwnerId : std::wstring{});
        m_libraryDetailPageLoading = true;
        auto pageLoadId = ++m_libraryDetailPageLoadId;

        // Only the first page gets a placeholder. This same coroutine appends
        // later pages as the user scrolls, and replacing rows they are already
        // reading with grey blocks would be a step backwards. Scoped because it
        // returns early on a stale epoch and on a superseded account playlist.
        LastMusicPlayer::Frontend::SkeletonLoadScope detailSkeleton{
            m_libraryDetailSkeleton,
            offset == 0
        };

        co_await winrt::resume_background();
        auto page = DatabaseService().LoadTrackPage(query);

        co_await wil::resume_foreground(dispatcher);
        if (epoch != m_libraryDetailHydrationEpoch || pageLoadId != m_libraryDetailPageLoadId)
        {
            if (pageLoadId == m_libraryDetailPageLoadId)
            {
                m_libraryDetailPageLoading = false;
            }
            co_return;
        }
        if (accountContext && !IsCurrentAccountPlaylistDetailContext(*accountContext))
        {
            HideLibraryDetail();
            co_return;
        }

        m_libraryDetailMatchedCount = page.TotalCount;
        m_libraryDetailMatchedSeconds = page.TotalSeconds;
        for (auto const& source : page.Tracks)
        {
            auto copy = source;
            copy.Index(static_cast<int32_t>(m_libraryDetailAllResults.size() + 1));
            ResolveArtworkPresentation(copy, L"track");
            m_libraryDetailAllResults.push_back(copy);
            m_libraryDetailTracks.Append(copy);
        }
        if (pageLoadId == m_libraryDetailPageLoadId)
        {
            m_libraryDetailPageLoading = false;
        }
        m_libraryDetailState = LoadState::Loaded;

        auto subtitle = winrt::hstring(m_libraryDetailSubtitle);
        auto count = m_libraryDetailMatchedCount;
        LibraryDetailSubtitleText().Text(winrt::hstring(std::to_wstring(count) + (count == 1 ? L" song" : L" songs")) + (subtitle.empty() ? L"" : L" - " + subtitle));
        ApplyLibraryDetailPlaylistCollage();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadLibrarySongsQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;

        if (DatabaseService().IsInitialized())
        {
            auto query = CurrentLibrarySongsQuery(0, 0);
            co_await winrt::resume_background();
            tracks = DatabaseService().LoadTracksForQuery(query);
            co_await wil::resume_foreground(dispatcher);
        }
        else
        {
            tracks = m_librarySongAllResults;
        }

        if (tracks.empty())
        {
            co_return;
        }
        if (!clickedTrack)
        {
            clickedTrack = tracks.front();
        }
        QueueAndPlayVisible(tracks, clickedTrack);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadLibraryDetailQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo clickedTrack)
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        auto epoch = m_libraryDetailHydrationEpoch;
        std::optional<AccountPlaylistDetailContext> accountContext;
        if (IsAccountPlaylistDetail())
        {
            accountContext = CaptureAccountPlaylistDetailContext();
            if (!accountContext)
            {
                HideLibraryDetail();
                co_return;
            }
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;
        if (m_libraryDetailKind == L"auto-playlist")
        {
            tracks = m_libraryDetailAllResults;
        }
        else if (DatabaseService().IsInitialized())
        {
            auto query = CurrentLibraryDetailQuery(
                0,
                0,
                accountContext ? accountContext->Binding.OwnerId : std::wstring{});
            co_await winrt::resume_background();
            tracks = DatabaseService().LoadTracksForQuery(query);
            co_await wil::resume_foreground(dispatcher);
        }
        else
        {
            tracks = m_libraryDetailAllResults;
        }

        if (epoch != m_libraryDetailHydrationEpoch)
        {
            co_return;
        }
        if (accountContext && !IsCurrentAccountPlaylistDetailContext(*accountContext))
        {
            HideLibraryDetail();
            co_return;
        }
        if (tracks.empty())
        {
            co_return;
        }

        if (accountContext && clickedTrack)
        {
            auto clickedRemoteId = clickedTrack.RemoteId();
            auto found = std::find_if(tracks.begin(), tracks.end(), [&](auto const& track)
            {
                return !clickedRemoteId.empty() && track.RemoteId() == clickedRemoteId;
            });
            if (found == tracks.end())
            {
                co_return;
            }
            clickedTrack = *found;
        }
        else if (!clickedTrack)
        {
            clickedTrack = tracks.front();
        }
        QueueAndPlayVisible(tracks, clickedTrack);
    }


    void MainWindow::AppendLibraryDetailPage()
    {
        AppendTrackPage(m_libraryDetailAllResults, m_libraryDetailTracks, kLibrarySongPageSize);
    }

    void MainWindow::MaybeAppendLibraryDetailPage(uint32_t itemIndex)
    {
        auto total = m_libraryDetailMatchedCount > 0 ? static_cast<uint32_t>(m_libraryDetailMatchedCount) : static_cast<uint32_t>(m_libraryDetailAllResults.size());
        if (m_libraryDetailTracks.Size() < total &&
            itemIndex + kPageAppendThreshold >= m_libraryDetailTracks.Size())
        {
            if (DatabaseService().IsInitialized() && m_libraryDetailKind != L"auto-playlist")
            {
                RunDetached(HydrateLibraryDetailTracksAsync());
            }
            else
            {
                AppendLibraryDetailPage();
            }
        }
    }



    void MainWindow::BindAccountPlaylist(
        winrt::Last_Music_Player::TrackInfo const& playlist,
        LastMusicPlayer::Backend::RemoteScopeSnapshot const& scope,
        std::wstring const& ownerId)
    {
        if (!playlist
            || playlist.Provider() != L"account"
            || playlist.RemoteId().empty()
            || scope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || ownerId.empty())
        {
            return;
        }

        auto existing = std::find_if(
            m_accountPlaylistBindings.begin(),
            m_accountPlaylistBindings.end(),
            [&](AccountPlaylistBinding const& binding)
            {
                return binding.Playlist == playlist;
            });
        if (existing != m_accountPlaylistBindings.end())
        {
            existing->Scope = scope;
            existing->OwnerId = ownerId;
            return;
        }
        m_accountPlaylistBindings.push_back({ playlist, scope, ownerId });
    }

    std::optional<MainWindow::AccountPlaylistBinding> MainWindow::AccountPlaylistBindingFor(
        winrt::Last_Music_Player::TrackInfo const& playlist) const
    {
        if (!playlist)
        {
            return std::nullopt;
        }
        auto found = std::find_if(
            m_accountPlaylistBindings.begin(),
            m_accountPlaylistBindings.end(),
            [&](AccountPlaylistBinding const& binding)
            {
                return binding.Playlist == playlist;
            });
        if (found == m_accountPlaylistBindings.end())
        {
            return std::nullopt;
        }
        return *found;
    }

    bool MainWindow::IsCurrentAccountPlaylistBinding(AccountPlaylistBinding const& binding)
    {
        auto snapshot = AccountSessionService().Snapshot();
        return binding.Playlist
            && !binding.OwnerId.empty()
            && binding.Scope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
            && snapshot.Generation == binding.Scope.AccountGeneration
            && snapshot.Profile.Id == winrt::hstring(binding.OwnerId)
            && DatabaseService().ActiveAccountId() == binding.OwnerId
            && RemoteMusicServiceService().IsCurrent(binding.Scope);
    }

    bool MainWindow::IsAccountPlaylistDetail() const noexcept
    {
        static constexpr wchar_t accountPrefix[] = L"account-playlist|";
        return m_libraryDetailKind == L"playlist"
            && m_libraryDetailKey.starts_with(accountPrefix);
    }

    std::optional<MainWindow::AccountPlaylistDetailContext>
        MainWindow::CaptureAccountPlaylistDetailContext()
    {
        if (!IsAccountPlaylistDetail() || !m_libraryDetailAccountBinding)
        {
            return std::nullopt;
        }

        static constexpr wchar_t accountPrefix[] = L"account-playlist|";
        auto playlistId = m_libraryDetailKey.substr(std::size(accountPrefix) - 1);
        auto binding = *m_libraryDetailAccountBinding;
        if (playlistId.empty()
            || !IsCurrentAccountPlaylistBinding(binding)
            || binding.Playlist.RemoteId() != winrt::hstring(playlistId))
        {
            return std::nullopt;
        }

        return AccountPlaylistDetailContext{
            std::move(binding),
            m_libraryDetailKey,
            std::move(playlistId),
            m_libraryDetailHydrationEpoch
        };
    }

    bool MainWindow::IsCurrentAccountPlaylistDetailContext(
        AccountPlaylistDetailContext const& context)
    {
        if (context.DetailEpoch != m_libraryDetailHydrationEpoch
            || context.DetailKey != m_libraryDetailKey
            || context.PlaylistId.empty()
            || !IsAccountPlaylistDetail()
            || !m_libraryDetailAccountBinding)
        {
            return false;
        }

        auto const& current = *m_libraryDetailAccountBinding;
        return current.Playlist == context.Binding.Playlist
            && current.OwnerId == context.Binding.OwnerId
            && current.Scope.Mode == context.Binding.Scope.Mode
            && current.Scope.Generation == context.Binding.Scope.Generation
            && current.Scope.AccountGeneration == context.Binding.Scope.AccountGeneration
            && current.Scope.CachePartition == context.Binding.Scope.CachePartition
            && current.Playlist.RemoteId() == winrt::hstring(context.PlaylistId)
            && IsCurrentAccountPlaylistBinding(current);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibraryGroup_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto group = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!group)
        {
            co_return;
        }

        auto kind = group.SourceKind();
        if (kind.empty())
        {
            kind = L"album";
        }
        if (kind == L"playlist" && group.Provider() == L"account")
        {
            auto binding = AccountPlaylistBindingFor(group);
            if (!binding || !IsCurrentAccountPlaylistBinding(*binding))
            {
                MarkLibraryViewsDirty();
                co_await HydrateLibraryTabAsync(L"Playlists", true);
                co_return;
            }
        }
        auto subtitle = (kind == L"album" || kind == L"album-collection" || kind == L"playlist" || kind == L"auto-playlist") ? group.Artist() : winrt::hstring{};
        ShowLibraryDetail(
            kind,
            group.SourceUrl().empty() ? group.Title() : group.SourceUrl(),
            group.Title(),
            subtitle,
            ApprovedDetailArtwork(group, kind),
            group);
        co_return;
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SidebarPlaylist_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        (void)sender;
        auto playlist = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!playlist)
        {
            co_return;
        }
        if (playlist.Provider() == L"account")
        {
            auto binding = AccountPlaylistBindingFor(playlist);
            if (!binding || !IsCurrentAccountPlaylistBinding(*binding))
            {
                MarkLibraryViewsDirty();
                co_await HydrateLibraryTabAsync(L"Playlists", true);
                co_return;
            }
        }

        LibraryButton_Click(nullptr, nullptr);
        if (LibTabPlaylists())
        {
            LibTabPlaylists().IsChecked(true);
            LibraryTab_Checked(LibTabPlaylists(), nullptr);
        }
        ShowLibraryDetail(
            L"playlist",
            playlist.SourceUrl(),
            playlist.Title(),
            playlist.Artist(),
            ApprovedDetailArtwork(playlist, L"playlist"),
            playlist);
        co_return;
    }

    void MainWindow::ShowLibraryDetail(
        winrt::hstring const& kind,
        winrt::hstring const& key,
        winrt::hstring const& title,
        winrt::hstring const& subtitle,
        winrt::Microsoft::UI::Xaml::Media::ImageSource const& fallbackArt,
        winrt::Last_Music_Player::TrackInfo const& sourceGroup)
    {
        auto accountArtworkUrl = sourceGroup
            ? NormalizeMusicArtworkUrl(sourceGroup.ArtworkUrl())
            : winrt::hstring{};
        auto previousKind = m_libraryDetailKind;
        auto previousKey = m_libraryDetailKey;
        auto requestedKind = std::wstring(kind.c_str());
        auto requestedKey = std::wstring(key.c_str());
        auto sameDetail = previousKind == requestedKind
            && previousKey == requestedKey;
        static constexpr wchar_t accountPrefix[] = L"account-playlist|";
        auto accountDetail = requestedKind == L"playlist"
            && requestedKey.starts_with(accountPrefix);
        if (sourceGroup && sourceGroup.Provider() == L"account" && !accountDetail)
        {
            return;
        }
        if (accountDetail)
        {
            std::optional<AccountPlaylistBinding> binding;
            if (sourceGroup)
            {
                if (sourceGroup.Provider() != L"account")
                {
                    return;
                }
                binding = AccountPlaylistBindingFor(sourceGroup);
            }
            else if (sameDetail)
            {
                binding = m_libraryDetailAccountBinding;
            }

            auto playlistId = requestedKey.substr(std::size(accountPrefix) - 1);
            if (!binding
                || playlistId.empty()
                || !IsCurrentAccountPlaylistBinding(*binding)
                || binding->Playlist.RemoteId() != winrt::hstring(playlistId))
            {
                if (sameDetail && IsAccountPlaylistDetail())
                {
                    HideLibraryDetail();
                }
                return;
            }
            m_libraryDetailAccountBinding = std::move(binding);
        }
        else if (requestedKind != L"playlist" || !sameDetail)
        {
            m_libraryDetailAccountBinding.reset();
        }

        auto detailFallbackArt = fallbackArt;
        auto checkedLoadedGroupArt = false;
        auto matchesDetailGroup = [&](winrt::Last_Music_Player::TrackInfo const& group)
        {
            auto groupKey = group.SourceUrl().empty() ? group.Title() : group.SourceUrl();
            return groupKey == key || group.SourceUrl() == key || group.Title() == key || (!title.empty() && group.Title() == title);
        };
        auto findLoadedGroupArt = [&]() -> winrt::Microsoft::UI::Xaml::Media::ImageSource
        {
            auto findIn = [&](auto const& groups, winrt::hstring const& context) -> winrt::Microsoft::UI::Xaml::Media::ImageSource
            {
                checkedLoadedGroupArt = true;
                for (uint32_t i = 0; i < groups.Size(); ++i)
                {
                    auto group = groups.GetAt(i);
                    if (matchesDetailGroup(group))
                    {
                        return ApprovedDetailArtwork(group, context);
                    }
                }
                return nullptr;
            };

            if (kind == L"album" || kind == L"album-collection")
            {
                return findIn(m_albums, kind);
            }
            if (kind == L"artist")
            {
                return findIn(m_artists, kind);
            }
            if (kind == L"genre")
            {
                return findIn(m_libraryGenres, kind);
            }
            if (kind == L"playlist")
            {
                return findIn(m_manualPlaylists, kind);
            }
            if (kind == L"auto-playlist")
            {
                return findIn(m_autoPlaylists, kind);
            }
            return nullptr;
        };

        if (!detailFallbackArt)
        {
            detailFallbackArt = findLoadedGroupArt();
        }

        if (detailFallbackArt)
        {
            m_libraryDetailFallbackArt = detailFallbackArt;
        }
        else if (!checkedLoadedGroupArt && previousKind == std::wstring(kind.c_str()) && previousKey == std::wstring(key.c_str()))
        {
            detailFallbackArt = m_libraryDetailFallbackArt;
        }
        else
        {
            m_libraryDetailFallbackArt = nullptr;
        }

        m_libraryDetailKind = std::move(requestedKind);
        m_libraryDetailKey = std::move(requestedKey);
        m_libraryDetailSubtitle = std::wstring(subtitle.c_str());
        ++m_libraryDetailHydrationEpoch;

        m_libraryDetailTracks.Clear();
        m_libraryDetailAllResults.clear();
        m_libraryDetailMatchedCount = 0;
        m_libraryDetailMatchedSeconds = 0.0;
        m_libraryDetailPageLoading = false;
        ++m_libraryDetailPageLoadId;
        m_libraryDetailState = LoadState::Loading;
        if (m_libraryDetailKind == L"auto-playlist")
        {
            auto mixIt = m_homeMixes.find(m_libraryDetailKey);
            if (mixIt != m_homeMixes.end())
            {
                int index = 1;
                for (auto const& source : mixIt->second)
                {
                    auto track = source;
                    track.Index(index++);
                    m_libraryDetailAllResults.push_back(track);
                }
            }
            m_libraryDetailMatchedCount = static_cast<int>(m_libraryDetailAllResults.size());
            m_libraryDetailState = LoadState::Loaded;
            AppendLibraryDetailPage();
        }
        else
        {
            RunDetached(HydrateLibraryDetailTracksAsync());
        }

        LibraryHeaderBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryNavigationBar().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryTabsContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryDetailContent().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        UpdateLibraryActionButtons();
        LibraryDetailKindText().Text(kind == L"artist" ? L"ARTIST" : (kind == L"genre" ? L"GENRE" : ((kind == L"playlist" || kind == L"auto-playlist") ? L"PLAYLIST" : L"ALBUM")));
        LibraryDetailTitleText().Text(title);
        auto count = m_libraryDetailMatchedCount;
        LibraryDetailSubtitleText().Text(count > 0
            ? winrt::hstring(std::to_wstring(count) + (count == 1 ? L" song" : L" songs")) + (subtitle.empty() ? L"" : L" - " + subtitle)
            : winrt::hstring{ L"Loading songs..." });
        // Invalidates any relay response started for the previously open detail.
        LibraryDetailArt().Tag(nullptr);
        LibraryDetailArt().Source(nullptr);
        LibraryDetailArt().Opacity(detailFallbackArt ? 1.0 : 0.0);
        LibraryDetailGeneratedArtwork().Opacity(detailFallbackArt ? 0.0 : 1.0);
        LibraryDetailFallbackIcon().Opacity(0.0);
        LibraryDetailGeneratedGlyph().Glyph(L"\xE8D6");
        LibraryDetailGeneratedTitle().Text(UpperArtworkText(title, (kind == L"playlist" || kind == L"auto-playlist") ? winrt::hstring{ L"MUSIC" } : winrt::hstring{ L"ALBUM" }));
        LibraryDetailGeneratedCaption().Text(subtitle.empty() ? ((kind == L"playlist" || kind == L"auto-playlist") ? winrt::hstring{ L"Playlist collection" } : winrt::hstring{ L"Album collection" }) : subtitle);
        if (detailFallbackArt)
        {
            LibraryDetailArt().Source(detailFallbackArt);
        }
        if (!accountArtworkUrl.empty() && IsHttpUrl(accountArtworkUrl))
        {
            // The detail header artwork is 168 effective pixels, inside the
            // range the tile decode covers.
            QueueAccountArtworkImage(LibraryDetailArt(), accountArtworkUrl, ArtworkDetail::Tile, sourceGroup);
        }
        ApplyLibraryDetailPlaylistCollage();
    }

    void MainWindow::ApplyLibraryDetailPlaylistCollage()
    {
        auto collage = LibraryDetailCollageArtwork();
        if (!collage)
        {
            return;
        }

        auto slots = std::array{
            LibraryDetailCollageArt0(),
            LibraryDetailCollageArt1(),
            LibraryDetailCollageArt2(),
            LibraryDetailCollageArt3()
        };
        for (auto const& slot : slots)
        {
            if (slot)
            {
                slot.Source(nullptr);
            }
        }
        collage.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        auto isImportedPlaylist =
            m_libraryDetailKind == L"playlist" &&
            m_libraryDetailKey.rfind(L"import-playlist|", 0) == 0;
        if (!isImportedPlaylist)
        {
            return;
        }

        std::vector<winrt::Microsoft::UI::Xaml::Media::ImageSource> art;
        std::unordered_set<std::wstring> seen;
        for (auto const& source : m_libraryDetailAllResults)
        {
            auto track = source;
            ResolveArtworkPresentation(track, L"track");
            if (track.ImageArtworkOpacity() <= 0.0 || track.AlbumArt() == nullptr)
            {
                continue;
            }

            auto key = std::wstring(track.ArtworkUrl().c_str());
            if (key.empty())
            {
                key = std::wstring(track.Title().c_str()) + L"|" + std::wstring(track.Artist().c_str());
            }
            if (!seen.insert(key).second)
            {
                continue;
            }

            art.push_back(track.AlbumArt());
            if (art.size() == slots.size())
            {
                break;
            }
        }

        if (art.size() < 2)
        {
            return;
        }

        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (slots[i])
            {
                slots[i].Source(art[i % art.size()]);
            }
        }

        collage.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        LibraryDetailArt().Opacity(0.0);
        LibraryDetailGeneratedArtwork().Opacity(0.0);
        LibraryDetailFallbackIcon().Opacity(0.0);
    }

    void MainWindow::HideLibraryDetail()
    {
        ++m_libraryDetailHydrationEpoch;
        ++m_libraryDetailPageLoadId;
        m_libraryDetailKind.clear();
        m_libraryDetailKey.clear();
        m_libraryDetailSubtitle.clear();
        m_libraryDetailAccountBinding.reset();
        m_libraryDetailFallbackArt = nullptr;
        m_libraryDetailTracks.Clear();
        m_libraryDetailAllResults.clear();
        m_libraryDetailMatchedCount = 0;
        m_libraryDetailMatchedSeconds = 0.0;
        m_libraryDetailPageLoading = false;
        if (auto detail = LibraryDetailContent())
        {
            detail.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
        if (auto tabs = LibraryTabsContent())
        {
            tabs.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        if (auto header = LibraryHeaderBar())
        {
            header.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        if (auto navigation = LibraryNavigationBar())
        {
            navigation.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }
        UpdateLibraryActionButtons();
    }

    void MainWindow::LibraryDetailBack_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        HideLibraryDetail();
    }

    void MainWindow::LibraryDetailPlay_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_libraryDetailAllResults.empty() || m_libraryDetailMatchedCount > 0)
        {
            RunDetached(LoadLibraryDetailQueueAndPlayAsync(winrt::Last_Music_Player::TrackInfo{ nullptr }));
        }
    }

    void MainWindow::LibraryDetailShuffle_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (!m_libraryDetailAllResults.empty() || m_libraryDetailMatchedCount > 0)
        {
            RunDetached(LoadLibraryDetailQueueAndShuffleAsync());
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LoadLibraryDetailQueueAndShuffleAsync()
    {
        auto lifetime = get_strong();
        auto dispatcher = this->DispatcherQueue();
        auto epoch = m_libraryDetailHydrationEpoch;
        std::optional<AccountPlaylistDetailContext> accountContext;
        if (IsAccountPlaylistDetail())
        {
            accountContext = CaptureAccountPlaylistDetailContext();
            if (!accountContext)
            {
                HideLibraryDetail();
                co_return;
            }
        }

        std::vector<winrt::Last_Music_Player::TrackInfo> tracks;

        // Same source-resolution as LoadLibraryDetailQueueAndPlayAsync —
        // auto-playlists are synthesised in m_libraryDetailAllResults;
        // every other detail kind loads the full set fresh from the DB.
        if (m_libraryDetailKind == L"auto-playlist")
        {
            tracks = m_libraryDetailAllResults;
        }
        else if (DatabaseService().IsInitialized())
        {
            auto query = CurrentLibraryDetailQuery(
                0,
                0,
                accountContext ? accountContext->Binding.OwnerId : std::wstring{});
            co_await winrt::resume_background();
            tracks = DatabaseService().LoadTracksForQuery(query);
            co_await wil::resume_foreground(dispatcher);
        }
        else
        {
            tracks = m_libraryDetailAllResults;
        }

        if (epoch != m_libraryDetailHydrationEpoch)
        {
            co_return;
        }
        if (accountContext && !IsCurrentAccountPlaylistDetailContext(*accountContext))
        {
            HideLibraryDetail();
            co_return;
        }
        if (tracks.empty())
        {
            co_return;
        }

        // Pre-shuffle so the first played track is random AND the Up Next
        // rail (which walks the queue linearly via RebuildUpNextQueue)
        // visibly reflects the shuffle. EnsureShuffleOn keeps global
        // shuffle on for subsequent advances — same semantics as the
        // group-card Shuffle menu items.
        std::random_device rd;
        std::mt19937 gen(rd());
        std::shuffle(tracks.begin(), tracks.end(), gen);
        EnsureShuffleOn();
        QueueAndPlayVisible(tracks, tracks.front());
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::LibrarySongsListView_ItemClick(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ItemClickEventArgs const& args)
    {
        auto clickedTrack = args.ClickedItem().try_as<winrt::Last_Music_Player::TrackInfo>();
        if (!clickedTrack)
        {
            co_return;
        }

        if (sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ListView>() == LibraryDetailTracksListView())
        {
            co_await LoadLibraryDetailQueueAndPlayAsync(clickedTrack);
        }
        else
        {
            co_await LoadLibrarySongsQueueAndPlayAsync(clickedTrack);
        }
        co_return;
    }

    void MainWindow::LibraryDetailTracks_ContainerContentChanging(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::ContainerContentChangingEventArgs const& args)
    {
        (void)sender;
        if (!args.InRecycleQueue())
        {
            MaybeAppendLibraryDetailPage(args.ItemIndex());
        }
    }

}
