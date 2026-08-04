#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"

#include "Backend/AccountClient.h"
#include "Backend/ProviderClient.h"
#include "Backend/SettingsManager.h"
#include "Backend/TrayIcon.h"
#include "Backend/DiscordPresence.h"
#include "Backend/HistoryImport.h"

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
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cwctype>
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

    void MainWindow::InvalidateRemoteViewWork()
    {
        auto closeAccountDetail = IsAccountPlaylistDetail()
            || m_libraryDetailAccountBinding.has_value();
        ++m_homeHydration.HomeEpoch;
        ++m_homeHydration.MixRefreshId;
        ++m_songsHydrationEpoch;
        ++m_libraryHydrationEpoch;
        if (!closeAccountDetail)
        {
            ++m_libraryDetailHydrationEpoch;
        }
        ++m_searchDebounceId;
        ++m_searchRequestId;
        ++m_lyricsHydrationEpoch;
        ++m_autoplay.Epoch;
        ++m_discoverEpoch;
        ++m_remotePlaybackResolveEpoch;
        m_autoplay.InFlight = false;
        m_autoplay.ResumeWhenReady = false;
        m_remoteSearchCache.clear();
        m_accountPlaylistBindings.clear();
        if (closeAccountDetail)
        {
            HideLibraryDetail();
        }
        else
        {
            m_libraryDetailAccountBinding.reset();
        }
        MarkLibraryViewsDirty();
        ClearAccountArtworkCache();
        if (m_lyricsHydrationTimer)
        {
            m_lyricsHydrationTimer.Stop();
        }
        if (m_lyricsService)
        {
            m_lyricsService->ClearCache();
        }
    }

    void MainWindow::InvalidateRemoteScopeWork()
    {
        RemoteMusicServiceService().InvalidateScope();
        InvalidateRemoteViewWork();
        StreamCacheService().InvalidateInFlight();
        ++m_discoverEpoch;
        m_discoverLoaded = false;
        m_catalogDiscovery = {};
        m_catalogContentStorefront.clear();
        m_catalogBackStack.clear();
        m_catalogGalleryCharts.clear();
        m_catalogLikeOverrides.clear();
        ClearAccountArtworkCache();
        if (DiscoverChartGalleryPanel())
        {
            DiscoverChartGalleryPanel().Children().Clear();
            m_discoverChartItems.Clear();
            m_discoverDetailTracks.Clear();
        }
        if (HomeCatalogPrimaryPanel())
        {
            HomeCatalogPrimaryPanel().Children().Clear();
            HomeCatalogMoodPanel().Children().Clear();
            HomeCatalogPrimaryContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            HomeCatalogMoodPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    namespace
    {
        class UserDataAdmissionGuard final
        {
        public:
            explicit UserDataAdmissionGuard(
                LastMusicPlayer::Backend::UserDataOperationGate& gate) noexcept
                : m_gate(&gate)
            {
            }

            UserDataAdmissionGuard(UserDataAdmissionGuard const&) = delete;
            UserDataAdmissionGuard& operator=(UserDataAdmissionGuard const&) = delete;

            ~UserDataAdmissionGuard()
            {
                Reopen();
            }

            void Reopen() noexcept
            {
                if (!m_gate)
                {
                    return;
                }

                try
                {
                    m_gate->Reopen();
                }
                catch (...)
                {
                }
                m_gate = nullptr;
            }

        private:
            LastMusicPlayer::Backend::UserDataOperationGate* m_gate;
        };

        std::wstring NormalizeLegacyPlayedAt(std::wstring value)
        {
            if (value.size() == 19 && value[4] == L'-' && value[7] == L'-'
                && value[10] == L' ' && value[13] == L':' && value[16] == L':')
            {
                value[10] = L'T';
                value.push_back(L'Z');
                return value;
            }
            if (value.size() == 20 && value[10] == L'T' && value.back() == L'Z')
            {
                return value;
            }
            return {};
        }

        winrt::Last_Music_Player::TrackInfo ResolvedImportTrack(
            winrt::hstring const& payload,
            winrt::Last_Music_Player::TrackInfo const& legacy)
        {
            winrt::Windows::Data::Json::JsonObject root;
            if (!winrt::Windows::Data::Json::JsonObject::TryParse(payload, root) || !root)
            {
                return nullptr;
            }
            auto result = root.GetNamedObject(L"result", nullptr);
            if (!result)
            {
                return nullptr;
            }
            auto nested = result.GetNamedObject(L"track", nullptr);
            auto trackObject = nested ? nested : result;
            auto remoteId = trackObject.GetNamedString(
                L"id",
                trackObject.GetNamedString(L"remoteId", trackObject.GetNamedString(L"trackId", L"")));
            auto sourceUrl = trackObject.GetNamedString(
                L"sourceUrl",
                trackObject.GetNamedString(L"url", L""));
            if (remoteId.empty() || sourceUrl.empty())
            {
                return nullptr;
            }

            LastMusicPlayer::Backend::TrackInfo track;
            track.RemoteId(remoteId);
            track.SourceKind(L"remote");
            track.Provider(trackObject.GetNamedString(L"provider", legacy.Provider()));
            track.SourceLabel(L"Account");
            track.SourceUrl(sourceUrl);
            track.Title(trackObject.GetNamedString(L"title", trackObject.GetNamedString(L"name", legacy.Title())));
            track.Artist(trackObject.GetNamedString(L"artist", legacy.Artist()));
            track.Album(trackObject.GetNamedString(L"album", legacy.Album()));
            auto artworkUrl = trackObject.GetNamedString(
                L"artworkUrl",
                trackObject.GetNamedString(L"imageUrl", L""));
            if (artworkUrl.empty() && nested)
            {
                artworkUrl = result.GetNamedString(
                    L"artworkUrl",
                    result.GetNamedString(L"imageUrl", legacy.ArtworkUrl()));
            }
            track.ArtworkUrl(artworkUrl.empty() ? legacy.ArtworkUrl() : artworkUrl);
            auto durationSeconds = trackObject.GetNamedNumber(L"durationSeconds", legacy.DurationSeconds());
            auto durationMs = trackObject.GetNamedNumber(L"durationMs", 0.0);
            track.DurationSeconds(durationMs > 0.0 ? durationMs / 1000.0 : durationSeconds);
            track.Duration(track.DurationSeconds() > 0.0
                ? winrt::hstring(LastMusicPlayer::Frontend::UIHelpers::FormatTime(track.DurationSeconds()))
                : legacy.Duration());
            return IsCompatibleAccountRemoteTrack(track) ? track : nullptr;
        }

        bool IsSkippableLegacyResolveError(winrt::hresult_error const& error) noexcept
        {
            return error.code() == HRESULT_FROM_WIN32(ERROR_NOT_FOUND)
                || error.code() == E_INVALIDARG
                || error.code() == E_FAIL;
        }

    }

    void MainWindow::RefreshAccountSettingsUi()
    {
        using LastMusicPlayer::Backend::AccountSessionStatus;
        using LastMusicPlayer::Backend::RemoteAccessMode;
        using winrt::Microsoft::UI::Xaml::Visibility;

        auto& session = AccountSessionService();
        auto& remoteMusic = RemoteMusicServiceService();
        auto snapshot = session.Snapshot();
        auto status = snapshot.Status;
        auto profile = snapshot.Profile;
        auto available = session.IsAccountIntegrationAvailable();
        auto signedIn = status == AccountSessionStatus::Validated || status == AccountSessionStatus::Offline;

        winrt::hstring statusText;
        if (!available)
        {
            statusText = L"Account integration is unavailable in this build.";
        }
        else if (status == AccountSessionStatus::SigningIn)
        {
            statusText = L"Waiting for browser sign-in...";
        }
        else if (status == AccountSessionStatus::Validated)
        {
            statusText = L"Connected";
        }
        else if (status == AccountSessionStatus::Offline)
        {
            statusText = L"Offline. Showing the cached account library.";
        }
        else
        {
            statusText = snapshot.LastSafeError;
            if (statusText.empty())
            {
                statusText = L"Not signed in";
            }
        }
        AccountStatusText().Text(statusText);

        AccountSignInButton().IsEnabled(available && status != AccountSessionStatus::SigningIn);
        AccountSignInButton().Content(winrt::box_value(winrt::hstring{ L"Sign in" }));
        AccountSignInButton().Visibility(signedIn ? Visibility::Collapsed : Visibility::Visible);
        AccountSignOutButton().Visibility(signedIn ? Visibility::Visible : Visibility::Collapsed);
        AccountSignOutButton().IsEnabled(signedIn);
        AccountSyncButton().Visibility(signedIn ? Visibility::Visible : Visibility::Collapsed);
        AccountSyncButton().IsEnabled(status == AccountSessionStatus::Validated && !MusicSyncServiceService().IsSyncing());
        // Which identity the app is presenting decides which half of the
        // identity card is on screen: the rename box, or the read-only account
        // profile. Never both, and never neither.
        auto identity = detail::ResolveProfileIdentity();
        auto usingAccountIdentity =
            identity.Source == LastMusicPlayer::Backend::ProfileIdentitySource::Account;
        SettingsIdentityAccountPanel().Visibility(
            usingAccountIdentity ? Visibility::Visible : Visibility::Collapsed);
        SettingsIdentityManualPanel().Visibility(
            usingAccountIdentity ? Visibility::Collapsed : Visibility::Visible);

        // Hidden rather than disabled when this build has no trusted frontend
        // origin: there is nothing to manage and no page to send the user to.
        AccountManageButton().Visibility(
            signedIn && !LastMusicPlayer::Backend::AccountManagementUrl().empty()
            ? Visibility::Visible
            : Visibility::Collapsed);

        auto displayName = profile.DisplayName.empty() ? profile.Username : profile.DisplayName;
        AccountDisplayNameText().Text(displayName.empty() ? winrt::hstring{ L"Account" } : displayName);
        AccountUsernameText().Text(profile.Username.empty()
            ? winrt::hstring{}
            : winrt::hstring(L"@" + std::wstring(profile.Username.c_str())));
        AccountPlanText().Text(profile.PlanLabel.empty() ? winrt::hstring{ L"Account library" } : profile.PlanLabel);

        // Offline still shows the cached picture: it is the same profile the
        // rest of the shell is presenting, and a cached bitmap needs no network.
        detail::ApplyAvatarPicture(
            AccountAvatarImage(),
            AccountAvatarFallback(),
            signedIn ? profile.AvatarUrl : winrt::hstring{});

        bool hasCachedData = false;
        if (DatabaseService().IsInitialized() && !profile.Id.empty())
        {
            auto ownerId = std::wstring(profile.Id.c_str());
            auto syncState = DatabaseService().LoadAccountSyncState(ownerId);
            AccountLastSyncText().Text(syncState.LastSuccessfulSyncUtc.empty()
                ? winrt::hstring{ L"Not synchronized yet" }
                : winrt::hstring(L"Last synchronized " + syncState.LastSuccessfulSyncUtc));
            auto cached = DatabaseService().LoadAccountLibrary(ownerId);
            hasCachedData = cached && (!cached->Tracks.empty() || !cached->Playlists.empty())
                || !DatabaseService().LoadPendingPlaybackEvents(ownerId, 1).empty()
                || !DatabaseService().LoadPendingLikes(ownerId, 1).empty();
        }
        else
        {
            AccountLastSyncText().Text(L"Not synchronized yet");
        }
        AccountClearDataButton().IsEnabled(hasCachedData);

        // Home catalog content depends on both the mode and a live session, so
        // refresh it from the same place as the rest of the mode-driven state.
        UpdateCatalogAvailability();

        RemoteModeAccount().IsEnabled(available && signedIn);
        // Keep configuration reachable without reading the provider credential
        // while another mode is active. SetMode validates it on user selection.
        RemoteModeApiKey().IsEnabled(true);
        m_suppressRemoteModeChange = true;
        switch (remoteMusic.Mode())
        {
        case RemoteAccessMode::Account:
            RemoteModeSelector().SelectedIndex(1);
            break;
        case RemoteAccessMode::ApiKey:
            RemoteModeSelector().SelectedIndex(2);
            break;
        default:
            RemoteModeSelector().SelectedIndex(0);
            break;
        }
        m_suppressRemoteModeChange = false;
        if (LibraryScopeSelector())
        {
            auto checked = [](winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton const& button)
            {
                if (!button) return false;
                auto value = button.IsChecked();
                return value && value.Value();
            };
            auto scopedTab = checked(LibTabSongs()) || checked(LibTabHistory()) || checked(LibTabPlaylists());
            LibraryScopeSelector().Visibility(
                remoteMusic.Mode() == RemoteAccessMode::Account && signedIn && scopedTab
                ? Visibility::Visible
                : Visibility::Collapsed);
        }

        // Every sign-in, sign-out, mode change, restore, sync and wipe already
        // routes through here, so this is the one place the shell identity has
        // to be re-applied from.
        ApplyUserDisplayName();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RestoreAccountIntegrationAsync()
    {
        auto lifetime = get_strong();
        auto operationGeneration = UserDataOperationGateService().Generation();
        auto dispatcher = DispatcherQueue();
        co_await AccountSessionService().RestoreAsync();
        co_await wil::resume_foreground(dispatcher);

        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease || !UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            RefreshAccountSettingsUi();
            co_return;
        }
        auto snapshot = AccountSessionService().Snapshot();
        if (snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::SignedOut
            && RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
            DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
        }
        operationLease.reset();
        if (snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
            && RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit);

            // Restoring a session moves the database scope from "no account" to
            // the signed-in one, and the startup hydration already ran against
            // the old scope: its history query saw nothing, so Listen Again came
            // up empty. SynchronizeAccountLibraryAsync only rebuilds the views
            // when the sync itself succeeds, which leaves a failed or offline
            // sync showing that empty state until some later navigation happens
            // to hydrate again. The cached account library is readable either
            // way, so the views are rebuilt here regardless of the outcome.
            MarkLibraryViewsDirty();
            co_await HydrateHomeAsync(false);
        }
        else
        {
            RefreshAccountSettingsUi();
            MarkLibraryViewsDirty();
            UpdateSongsScopeLabel();
            co_await HydrateHomeAsync(false);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::SynchronizeAccountLibraryAsync(AccountSyncMode mode)
    {
        auto lifetime = get_strong();
        auto const background = mode == AccountSyncMode::Background;
        if (mode == AccountSyncMode::Interactive)
        {
            AccountStatusText().Text(L"Synchronizing account library...");
        }
        AccountSyncButton().IsEnabled(false);

        auto synchronized = co_await MusicSyncServiceService().SyncAsync();
        if (synchronized)
        {
            // Clearing the bindings orphans an open account playlist, so the
            // interactive paths close that view first. A background sync is
            // only ever started when no such view is open (EvaluateAutoSync
            // refuses otherwise), so it has nothing to close.
            if (!background && (IsAccountPlaylistDetail() || m_libraryDetailAccountBinding))
            {
                HideLibraryDetail();
            }
            m_accountPlaylistBindings.clear();
            MarkLibraryViewsDirty();
            m_remoteSearchCache.clear();

            auto remoteScope = RemoteMusicServiceService().CaptureScope();
            auto accountSnapshot = AccountSessionService().Snapshot();
            auto accountOwnerId = std::wstring(accountSnapshot.Profile.Id.c_str());
            auto accountScopeCurrent = !accountOwnerId.empty()
                && remoteScope.Mode == LastMusicPlayer::Backend::RemoteAccessMode::Account
                && remoteScope.AccountGeneration == accountSnapshot.Generation
                && DatabaseService().ActiveAccountId() == accountOwnerId
                && RemoteMusicServiceService().IsCurrent(remoteScope);

            m_sidebarPlaylists.Clear();
            for (auto const& playlist : DatabaseService().LoadRecentPlaylists(4))
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

            // Rebuilding a list replaces its ItemsSource, which sends the view
            // back to the top. A background sync therefore only rebuilds what
            // the user is not currently reading; MarkLibraryViewsDirty above
            // already makes everything else reload on next navigation.
            if (!background || m_currentNav != L"Home")
            {
                co_await HydrateHomeAsync(false);
            }

            if (!background)
            {
                co_await EnsureSongsHydratedAsync(true);

                struct LibraryTabRow
                {
                    winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton Button;
                    winrt::hstring Name;
                };
                LibraryTabRow tabs[] = {
                    { LibTabPlaylists(), L"Playlists" },
                    { LibTabHistory(), L"History" },
                    { LibTabAlbums(), L"Albums" },
                    { LibTabArtists(), L"Artists" },
                    { LibTabGenres(), L"Genres" },
                    { LibTabSongs(), L"Songs" },
                };
                for (auto const& tab : tabs)
                {
                    auto checked = tab.Button.IsChecked();
                    if (checked && checked.Value())
                    {
                        if (tab.Name != L"Songs")
                        {
                            co_await HydrateLibraryTabAsync(tab.Name, true);
                        }
                        break;
                    }
                }

                // Opens a modal dialog on first run. Never from a timer.
                co_await OfferCompatibleHistoryImportAsync();
            }
        }

        RefreshAccountSettingsUi();
        if (mode == AccountSyncMode::Interactive)
        {
            AccountStatusText().Text(synchronized
                ? winrt::hstring{ L"Account library synchronized" }
                : MusicSyncServiceService().LastSafeError());
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::RequestBackgroundAccountSyncAsync(
        LastMusicPlayer::Backend::AutoSyncTrigger trigger)
    {
        auto lifetime = get_strong();

        LastMusicPlayer::Backend::AutoSyncConditions conditions;
        conditions.Enabled = SettingsManagerService().GetBool(L"AutoSyncAccount", true);
        conditions.Mode = RemoteMusicServiceService().Mode();
        conditions.Status = AccountSessionService().Snapshot().Status;
        conditions.Syncing = MusicSyncServiceService().IsSyncing();
        conditions.WindowInteractive = m_hwnd
            && ::IsWindowVisible(m_hwnd)
            && !::IsIconic(m_hwnd);
        conditions.AccountDetailOpen = IsAccountPlaylistDetail()
            || m_libraryDetailAccountBinding.has_value();

        auto const now = std::chrono::steady_clock::now();
        if (m_lastAutoSyncAttempt)
        {
            conditions.SinceLastAttempt = now - *m_lastAutoSyncAttempt;
        }

        if (LastMusicPlayer::Backend::EvaluateAutoSync(conditions, trigger)
            != LastMusicPlayer::Backend::AutoSyncDecision::Run)
        {
            co_return;
        }

        // Stamped before the sync rather than after, so a long or hung sync
        // cannot let a burst of activations queue up behind it.
        m_lastAutoSyncAttempt = now;
        co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Background);
    }

    void MainWindow::ApplyAutoSyncSetting()
    {
        if (!m_autoSyncTimer)
        {
            return;
        }

        // The timer only gates the periodic poll. Whether a given tick actually
        // syncs is still EvaluateAutoSync's call, so there is no need to also
        // start and stop it as the mode or session changes.
        if (SettingsManagerService().GetBool(L"AutoSyncAccount", true))
        {
            if (!m_autoSyncTimer.IsRunning())
            {
                m_autoSyncTimer.Start();
            }
        }
        else if (m_autoSyncTimer.IsRunning())
        {
            m_autoSyncTimer.Stop();
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OfferCompatibleHistoryImportAsync()
    {
        auto lifetime = get_strong();
        auto operationGeneration = UserDataOperationGateService().Generation();
        auto remoteScope = RemoteMusicServiceService().CaptureScope();
        auto accountSnapshot = AccountSessionService().Snapshot();
        auto profile = accountSnapshot.Profile;
        if (profile.Id.empty()
            || accountSnapshot.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
            || remoteScope.Mode != LastMusicPlayer::Backend::RemoteAccessMode::Account
            || remoteScope.AccountGeneration != accountSnapshot.Generation
            || !RemoteMusicServiceService().IsCurrent(remoteScope))
        {
            co_return;
        }

        auto accountId = std::wstring(profile.Id.c_str());
        auto accountStillCurrent = [&]()
        {
            auto current = AccountSessionService().Snapshot();
            return current.Generation == accountSnapshot.Generation
                && current.Profile.Id == winrt::hstring(accountId)
                && current.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                && UserDataOperationGateService().IsCurrent(operationGeneration)
                && RemoteMusicServiceService().IsCurrent(remoteScope);
        };

        auto state = DatabaseService().LoadAccountSyncState(accountId);
        if (state.LegacyHistoryImportState == L"declined" || state.LegacyHistoryImportState == L"completed")
        {
            co_return;
        }

        if (state.LegacyHistoryImportState.empty())
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock message;
            message.Text(
                L"Import compatible listening history from this PC? Only remote listening entries that the account service can resolve will be uploaded. "
                L"Local files and paths stay private, and historical desktop play counts remain on this PC.");
            message.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);

            winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
            dialog.Title(winrt::box_value(winrt::hstring(L"Import listening history")));
            dialog.PrimaryButtonText(L"Import compatible history");
            dialog.CloseButtonText(L"Don't import");
            dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
            dialog.XamlRoot(this->Content().XamlRoot());

            auto decision = co_await dialog.ShowAsync();
            state.AccountId = accountId;
            state.LegacyHistoryImportState = decision == winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary
                ? L"accepted"
                : L"declined";
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease || !accountStillCurrent())
            {
                co_return;
            }
            auto stateSaved = DatabaseService().SaveAccountSyncState(state);
            operationLease.reset();
            if (!stateSaved || state.LegacyHistoryImportState == L"declined")
            {
                co_return;
            }
        }

        auto candidates = DatabaseService().LoadLegacyRemoteHistoryImportCandidates();
        std::size_t imported{};
        std::size_t skipped{};
        AccountStatusText().Text(L"Importing compatible listening history...");

        for (auto const& candidate : candidates)
        {
            if (!accountStillCurrent())
            {
                co_return;
            }

            if (!IsCompatibleAccountRemoteTrack(candidate.Track, false))
            {
                ++skipped;
                continue;
            }

            auto playedAt = NormalizeLegacyPlayedAt(candidate.LastPlayedUtc);
            auto eventId = LastMusicPlayer::Backend::CreateHistoryImportEventId(accountId, candidate.SourceKey);
            if (playedAt.empty() || eventId.empty())
            {
                ++skipped;
                continue;
            }

            winrt::Last_Music_Player::TrackInfo resolved{ nullptr };
            try
            {
                auto payload = co_await RemoteMusicServiceService().ResolveUrlAsync(candidate.Track.SourceUrl());
                resolved = ResolvedImportTrack(payload, candidate.Track);
            }
            catch (winrt::hresult_error const& error)
            {
                if (IsSkippableLegacyResolveError(error))
                {
                    ++skipped;
                    continue;
                }
                AccountStatusText().Text(L"History import paused. It will resume on the next synchronization.");
                co_return;
            }
            catch (...)
            {
                AccountStatusText().Text(L"History import paused. It will resume on the next synchronization.");
                co_return;
            }
            if (!accountStillCurrent())
            {
                co_return;
            }


            if (!resolved)
            {
                ++skipped;
                continue;
            }

            LastMusicPlayer::Backend::PlaybackEventRecord event;
            event.EventId = eventId;
            event.AccountId = accountId;
            event.RemoteTrackId = std::wstring(resolved.RemoteId().c_str());
            event.Track = resolved;
            event.PlayedAtUtc = playedAt;
            event.PositionSeconds = 0.0;
            auto operationLease = UserDataOperationGateService().TryEnter();
            if (!operationLease || !accountStillCurrent())
            {
                co_return;
            }
            auto queued = DatabaseService().EnqueuePlaybackEvent(event);
            operationLease.reset();
            if (!queued)
            {
                AccountStatusText().Text(L"History import paused. It will resume on the next synchronization.");
                co_return;
            }
            ++imported;
        }

        state = DatabaseService().LoadAccountSyncState(accountId);
        state.AccountId = accountId;
        state.LegacyHistoryImportState = L"completed";
        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease || !accountStillCurrent())
        {
            co_return;
        }
        auto stateSaved = DatabaseService().SaveAccountSyncState(state);
        operationLease.reset();
        if (!stateSaved)
        {
            AccountStatusText().Text(L"History import paused. It will resume on the next synchronization.");
            co_return;
        }

        if (imported > 0)
        {
            co_await MusicSyncServiceService().SyncAsync();
            MarkLibraryViewsDirty();
            co_await HydrateHomeAsync(false);
        }

        winrt::Microsoft::UI::Xaml::Controls::TextBlock summary;
        summary.Text(winrt::hstring(
            std::to_wstring(imported) + L" compatible track" + (imported == 1 ? L" was" : L"s were")
            + L" queued for account history. " + std::to_wstring(skipped)
            + L" local or unresolved entr" + (skipped == 1 ? L"y was" : L"ies were") + L" left on this PC."));
        summary.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog resultDialog;
        resultDialog.Title(winrt::box_value(winrt::hstring(L"History import complete")));
        resultDialog.Content(summary);
        resultDialog.CloseButtonText(L"Done");
        resultDialog.XamlRoot(this->Content().XamlRoot());
        co_await resultDialog.ShowAsync();
    }


    winrt::Windows::Foundation::IAsyncAction MainWindow::AccountSync_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Interactive);
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AccountManage_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto url = LastMusicPlayer::Backend::AccountManagementUrl();
        if (url.empty())
        {
            co_return;
        }
        try
        {
            co_await winrt::Windows::System::Launcher::LaunchUriAsync(
                winrt::Windows::Foundation::Uri{ url });
        }
        catch (...)
        {
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AccountSignIn_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto operationGeneration = UserDataOperationGateService().Generation();
        AccountSignInButton().IsEnabled(false);
        AccountStatusText().Text(L"Opening browser sign-in...");

        auto signedIn = co_await AccountSessionService().SignInAsync();
        auto snapshot = AccountSessionService().Snapshot();
        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease || !UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            RefreshAccountSettingsUi();
            co_return;
        }
        if (signedIn
            && snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
            && !snapshot.Profile.Id.empty())
        {
            try
            {
                auto session = AccountSessionService().CaptureOperation();
                if (session.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
                    || session.Generation != snapshot.Generation
                    || session.OwnerId != snapshot.Profile.Id)
                {
                    throw winrt::hresult_canceled();
                }
                if (!RemoteMusicServiceService().SetMode(
                    LastMusicPlayer::Backend::RemoteAccessMode::Account))
                {
                    throw winrt::hresult_error(E_NOT_VALID_STATE, L"Account mode is unavailable.");
                }

                InvalidateRemoteScopeWork();
                auto context = RemoteMusicServiceService().CaptureAccountSyncContext();
                auto remoteScope = RemoteMusicServiceService().CaptureScope();
                if (context.OwnerId() != snapshot.Profile.Id
                    || remoteScope.AccountGeneration != snapshot.Generation
                    || !RemoteMusicServiceService().IsCurrent(context))
                {
                    throw winrt::hresult_canceled();
                }
                if (!DatabaseService().SetRemoteLibraryContext(
                    L"Account",
                    std::wstring(context.OwnerId().c_str())))
                {
                    throw winrt::hresult_error(E_FAIL, L"Could not activate the account library.");
                }

                UpdateSongsScopeLabel();
                operationLease.reset();
                co_await SynchronizeAccountLibraryAsync(AccountSyncMode::Interactive);
            }
            catch (winrt::hresult_canceled const&)
            {
                auto current = AccountSessionService().Snapshot();
                if ((current.Status != LastMusicPlayer::Backend::AccountSessionStatus::Validated
                        && current.Status != LastMusicPlayer::Backend::AccountSessionStatus::Offline)
                    || current.Profile.Id.empty())
                {
                    RemoteMusicServiceService().SetMode(
                        LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
                    InvalidateRemoteScopeWork();
                    (void)DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
                }
                RefreshAccountSettingsUi();
                co_return;
            }
            catch (...)
            {
                RemoteMusicServiceService().SetMode(
                    LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
                InvalidateRemoteScopeWork();
                (void)DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
                operationLease.reset();
                RefreshAccountSettingsUi();
                AccountStatusText().Text(L"Could not activate the account library.");
                co_return;
            }
        }
        RefreshAccountSettingsUi();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AccountSignOut_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto operationGeneration = UserDataOperationGateService().Generation();
        AccountSignOutButton().IsEnabled(false);
        AccountStatusText().Text(L"Signing out...");
        auto signedOut = co_await AccountSessionService().LogoutAsync();
        auto snapshot = AccountSessionService().Snapshot();
        auto operationLease = UserDataOperationGateService().TryEnter();
        if (!operationLease || !UserDataOperationGateService().IsCurrent(operationGeneration))
        {
            RefreshAccountSettingsUi();
            co_return;
        }
        if (signedOut && snapshot.Status == LastMusicPlayer::Backend::AccountSessionStatus::SignedOut)
        {
            RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
            InvalidateRemoteScopeWork();
            DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
            m_libraryScope = L"All";
            if (LibraryScopeSelector())
            {
                LibraryScopeSelector().SelectedIndex(0);
            }
            m_remoteSearchCache.clear();
            MarkLibraryViewsDirty();
            UpdateSongsScopeLabel();
            operationLease.reset();
            co_await HydrateHomeAsync(false);
        }
        RefreshAccountSettingsUi();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::AccountClearData_Click(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto lifetime = get_strong();
        auto profile = AccountSessionService().Snapshot().Profile;
        if (profile.Id.empty() || !DatabaseService().IsInitialized())
        {
            co_return;
        }

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(Content().XamlRoot());
        dialog.Title(winrt::box_value(L"Clear synced account data?"));
        dialog.Content(winrt::box_value(L"This removes the cached account library and any likes or listening history waiting to upload from this PC. Local music files are not changed."));
        dialog.PrimaryButtonText(L"Clear data");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        if (co_await dialog.ShowAsync() != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto dispatcher = DispatcherQueue();
        auto ownerId = std::wstring(profile.Id.c_str());
        if (!UserDataOperationGateService().CloseAdmissions())
        {
            AccountStatusText().Text(L"Another cleanup operation is already in progress.");
            co_return;
        }
        UserDataAdmissionGuard admissionGuard{ UserDataOperationGateService() };
        InvalidateRemoteViewWork();

        co_await winrt::resume_background();
        if (!UserDataOperationGateService().WaitForIdle(std::chrono::seconds(30)))
        {
            admissionGuard.Reopen();
            co_await wil::resume_foreground(dispatcher);
            AccountStatusText().Text(L"Could not stop a background operation. Try again shortly.");
            co_return;
        }

        auto currentAccount = AccountSessionService().Snapshot();
        if (currentAccount.Profile.Id != winrt::hstring(ownerId))
        {
            admissionGuard.Reopen();
            co_await wil::resume_foreground(dispatcher);
            AccountStatusText().Text(L"The active account changed. Nothing was cleared.");
            RefreshAccountSettingsUi();
            co_return;
        }

        auto keepActiveContext = RemoteMusicServiceService().Mode()
                == LastMusicPlayer::Backend::RemoteAccessMode::Account
            && (currentAccount.Status == LastMusicPlayer::Backend::AccountSessionStatus::Validated
                || currentAccount.Status == LastMusicPlayer::Backend::AccountSessionStatus::Offline);
        bool databaseCleared{};
        try
        {
            databaseCleared = DatabaseService().ClearAccountData(
                ownerId,
                keepActiveContext
                    ? LastMusicPlayer::Backend::AccountDataClearMode::RestoreActiveContext
                    : LastMusicPlayer::Backend::AccountDataClearMode::LeaveInactive);
        }
        catch (...)
        {
            databaseCleared = false;
        }
        admissionGuard.Reopen();
        co_await wil::resume_foreground(dispatcher);
        if (!databaseCleared)
        {
            AccountStatusText().Text(L"Could not clear all synced account data.");
            co_return;
        }
        MarkLibraryViewsDirty();
        RefreshAccountSettingsUi();
        co_await HydrateHomeAsync(false);
    }

    void MainWindow::RemoteMode_SelectionChanged(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_suppressRemoteModeChange)
        {
            return;
        }

        auto selected = RemoteModeSelector().SelectedItem().try_as<winrt::Microsoft::UI::Xaml::Controls::RadioButton>();
        if (!selected)
        {
            return;
        }
        auto tag = ReadTagString(selected.Tag());
        auto mode = LastMusicPlayer::Backend::ParseRemoteAccessMode(tag);
        if (!RemoteMusicServiceService().SetMode(mode))
        {
            RefreshAccountSettingsUi();
            return;
        }
        InvalidateRemoteScopeWork();

        if (mode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            auto snapshot = AccountSessionService().Snapshot();
            DatabaseService().SetRemoteLibraryContext(
                L"Account",
                std::wstring(snapshot.Profile.Id.c_str()));
        }
        else
        {
            DatabaseService().SetRemoteLibraryContext(std::wstring(LastMusicPlayer::Backend::RemoteAccessModeName(mode).c_str()));
        }
        if (mode != LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            m_libraryScope = L"All";
            if (LibraryScopeSelector())
            {
                LibraryScopeSelector().SelectedIndex(0);
            }
        }
        m_remoteSearchCache.clear();
        MarkLibraryViewsDirty();
        UpdateSongsScopeLabel();
        RefreshAccountSettingsUi();
        RunDetached(mode == LastMusicPlayer::Backend::RemoteAccessMode::Account
            ? SynchronizeAccountLibraryAsync(AccountSyncMode::Implicit)
            : HydrateHomeAsync(false));
    }

    void MainWindow::DisplayNameBox_TextChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto box = DisplayNameBox();
        auto btn = DisplayNameSaveButton();
        if (!box || !btn) return;
        // Dirty = current text (trimmed) differs from what's persisted.
        // Mirrors the trim semantics in DisplayNameSave_Click so leading
        // or trailing whitespace alone doesn't make the row look dirty.
        auto current = LastMusicPlayer::Backend::TrimProfileName(box.Text());
        auto persisted = LastMusicPlayer::Backend::TrimProfileName(SettingsManagerService().GetString(L"UserDisplayName", L""));
        btn.IsEnabled(current != persisted);
    }

    void MainWindow::DisplayNameSave_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        auto box = DisplayNameBox();
        if (!box) return;
        auto trimmed = LastMusicPlayer::Backend::TrimProfileName(box.Text());
        // Empty allowed — ApplyUserDisplayName falls back to "Listener".
        SettingsManagerService().SetString(L"UserDisplayName", trimmed);
        // Reflect the trimmed value back into the box so the user sees
        // exactly what was saved.
        if (box.Text() != trimmed)
        {
            box.Text(trimmed);
        }
        if (auto btn = DisplayNameSaveButton())
        {
            btn.IsEnabled(false);
        }
        ApplyUserDisplayName();
    }

    void MainWindow::DisplayNameBox_KeyDown(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& args)
    {
        (void)sender;
        if (args.Key() != winrt::Windows::System::VirtualKey::Enter)
        {
            return;
        }
        args.Handled(true);
        // Enter commits via the same path as the Save button so there's
        // only one persistence path.
        DisplayNameSave_Click(nullptr, winrt::Microsoft::UI::Xaml::RoutedEventArgs{});
    }

    void MainWindow::MainBodyGrid_SizeChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args)
    {
        (void)sender;
        ApplySettingsResponsiveLayout(args.NewSize().Width);
        ApplyRightRailWidth();
    }


    void MainWindow::ApplySettingsResponsiveLayout(double width)
    {
        constexpr double compactBreakpoint = 1500.0;
        bool compact = width < compactBreakpoint;

        using winrt::Microsoft::UI::Xaml::FrameworkElement;
        using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
        using winrt::Microsoft::UI::Xaml::Thickness;
        using winrt::Microsoft::UI::Xaml::UIElement;
        using winrt::Microsoft::UI::Xaml::Controls::Grid;

        auto placeAction = [&](UIElement const& element, int wideColumn, int compactColumn)
        {
            if (!element)
            {
                return;
            }

            if (auto fe = element.try_as<FrameworkElement>())
            {
                Grid::SetColumn(fe, compact ? compactColumn : wideColumn);
                Grid::SetRow(fe, compact ? 1 : 0);
                fe.HorizontalAlignment(compact ? HorizontalAlignment::Left : HorizontalAlignment::Stretch);
                fe.Margin(compact ? Thickness{ 0, 12, 0, 0 } : Thickness{ 0, 0, 0, 0 });
            }
        };

        placeAction(MusicFolderActions(), 2, 1);
        placeAction(ScanMusicButton(), 2, 1);
        placeAction(AutoplaySwitch(), 1, 0);
        placeAction(GaplessSwitch(), 1, 0);
        placeAction(CloseBehaviorCombo(), 1, 0);
        placeAction(OutputDeviceCombo(), 1, 0);
        placeAction(ThemeOptionsPanel(), 1, 0);
        placeAction(AccentSwatchesPanel(), 1, 0);
        placeAction(ShowAlbumArtSwitch(), 1, 0);
        placeAction(DiscordPresenceSwitch(), 1, 0);
        placeAction(WindowsMediaControlsSwitch(), 1, 0);
    }

    void MainWindow::ThemeSegment_Checked(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto clicked = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!clicked)
        {
            return;
        }
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton segs[] = {
            ThemeLight(), ThemeDark(), ThemeSystem()
        };
        for (auto const& s : segs)
        {
            if (s && s != clicked)
            {
                s.IsChecked(false);
            }
        }

        auto tag = ReadTagString(clicked.Tag());
        if (tag.empty())
        {
            tag = L"System";
        }
        auto theme = winrt::Microsoft::UI::Xaml::ElementTheme::Default;
        if (tag == L"Light")
        {
            theme = winrt::Microsoft::UI::Xaml::ElementTheme::Light;
        }
        else if (tag == L"Dark")
        {
            theme = winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
        }

        if (auto root = this->Content().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            root.RequestedTheme(theme);
        }
        // Captured brushes are theme-specific; force a re-capture on next use.
        m_accentBrushesCaptured = false;

        // Persist the choice (skipped while LoadSettingsIntoUi is syncing UI).
        if (!m_loadingSettings)
        {
            SettingsManagerService().SetString(L"Theme", tag);
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::OpenDataFolder_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        try
        {
            std::filesystem::create_directories(AppDataDirectory());
            auto localFolder = co_await winrt::Windows::Storage::StorageFolder::GetFolderFromPathAsync(
                winrt::hstring{ AppDataDirectory().wstring() });
            co_await winrt::Windows::System::Launcher::LaunchFolderAsync(localFolder);
        }
        catch (...) {}
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ShowLicenses_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::hstring credits =
            L"Last Music Player\n"
            L"© 2026 Debashis\n\n"
            L"Bundled components:\n"
            L"  • Windows App SDK 1.8 — MIT License\n"
            L"  • SQLite 3 — Public Domain\n"
            L"  • C++/WinRT — MIT License\n"
            L"  • Segoe Fluent Icons — Microsoft, bundled with Windows\n\n"
            L"This application is provided as-is, without warranty of any kind.";

        winrt::Microsoft::UI::Xaml::Controls::TextBlock body;
        body.Text(credits);
        body.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        body.FontSize(13.0);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dlg;
        dlg.Title(winrt::box_value(winrt::hstring(L"Licenses & credits")));
        dlg.Content(body);
        dlg.CloseButtonText(L"Close");
        dlg.XamlRoot(this->Content().XamlRoot());

        co_await dlg.ShowAsync();
    }

    // Maps a Settings control's x:Name to its persisted settings key.
    static winrt::hstring SettingKeyForControl(winrt::hstring const& name)
    {
        std::wstring n{ name.c_str() };
        if (n == L"ShowAlbumArtSwitch")        return L"ShowAlbumArt";
        if (n == L"WindowsMediaControlsSwitch")return L"WindowsMediaControls";
        if (n == L"GaplessSwitch")             return L"Gapless";
        if (n == L"AutoplaySwitch")            return L"Autoplay";
        if (n == L"DiscordPresenceSwitch")     return L"DiscordPresence";
        if (n == L"AutoSyncSwitch")            return L"AutoSyncAccount";
        if (n == L"CloseBehaviorCombo")        return L"CloseBehavior";
        if (n == L"OutputDeviceCombo")         return L"OutputDeviceIndex";
        return {};
    }

    void MainWindow::SettingsToggle_Toggled(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto ts = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch>();
        if (!ts)
        {
            return;
        }
        auto name = ts.Name();
        auto key = SettingKeyForControl(name);
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetBool(key, ts.IsOn());
        }
        if (name == L"ShowAlbumArtSwitch")
        {
            ApplyShowAlbumArt();
        }
        else if (name == L"WindowsMediaControlsSwitch")
        {
            ApplyWindowsMediaControls();
        }
        else if (name == L"DiscordPresenceSwitch")
        {
            ApplyDiscordPresence();
        }
        else if (name == L"AutoSyncSwitch")
        {
            ApplyAutoSyncSetting();
        }
        else if (name == L"GaplessSwitch")
        {
            ApplyGaplessSetting();
        }
        else if (name == L"AutoplaySwitch")
        {
            // Turning autoplay on should immediately start filling Up Next;
            // turning it off is honored lazily at the next end-of-queue.
            if (ts.IsOn())
            {
                MaybeExtendAutoplayQueue();
            }
        }
    }

    void MainWindow::SettingsCombo_SelectionChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args)
    {
        (void)args;
        auto cb = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::ComboBox>();
        if (!cb)
        {
            return;
        }
        if (cb.Name() == L"OutputDeviceCombo")
        {
            auto idx = cb.SelectedIndex();
            winrt::hstring deviceId =
                (idx > 0 && static_cast<size_t>(idx) < m_outputDeviceIds.size())
                    ? m_outputDeviceIds[static_cast<size_t>(idx)]
                    : winrt::hstring{};
            if (!m_loadingSettings)
            {
                SettingsManagerService().SetString(L"OutputDeviceId", deviceId);
            }
            ApplyOutputDeviceAsync();
            return;
        }

        auto key = SettingKeyForControl(cb.Name());
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetInt(key, cb.SelectedIndex());
        }
    }

    void MainWindow::SettingsSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        auto sl = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Slider>();
        if (!sl)
        {
            return;
        }
        auto key = SettingKeyForControl(sl.Name());
        if (!key.empty() && !m_loadingSettings)
        {
            SettingsManagerService().SetDouble(key, args.NewValue());
        }
    }

    void MainWindow::AccentSwatch_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& args)
    {
        (void)args;
        auto el = sender.try_as<winrt::Microsoft::UI::Xaml::Shapes::Ellipse>();
        if (!el)
        {
            return;
        }
        auto hex = ReadTagString(el.Tag());
        if (hex.empty())
        {
            return;
        }
        if (!m_loadingSettings)
        {
            SettingsManagerService().SetString(L"AccentColor", hex);
        }
        ApplyAccentColor(hex);
    }

    void MainWindow::ApplyShowAlbumArt()
    {
        bool show = SettingsManagerService().GetBool(L"ShowAlbumArt", true);
        if (auto c = BottomArtContainer())
        {
            c.Visibility(show ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                              : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    void MainWindow::ApplyWindowsMediaControls()
    {
        bool on = SettingsManagerService().GetBool(L"WindowsMediaControls", true);
        try
        {
            // Toggle the window-bound SMTC primarily — that's the one the
            // OS routes media keys to. The player-internal SMTC is kept
            // in sync too in case any UWP-side consumer reads it.
            auto smtc = m_windowSmtc
                ? m_windowSmtc
                : AudioPlayerService().GetMediaPlayer().SystemMediaTransportControls();
            smtc.IsEnabled(on);
        }
        catch (...)
        {
        }
    }

    void MainWindow::ApplyThemeFromSetting()
    {
        auto theme = SettingsManagerService().GetString(L"Theme", L"System");
        auto pick = (theme == L"Light") ? ThemeLight()
                  : (theme == L"Dark")  ? ThemeDark()
                                        : ThemeSystem();
        if (!pick)
        {
            return;
        }
        if (pick.IsChecked() && pick.IsChecked().Value())
        {
            // Already checked (e.g. the System default) — apply directly since
            // setting IsChecked(true) again would not raise Checked.
            ThemeSegment_Checked(pick, nullptr);
        }
        else
        {
            pick.IsChecked(true); // raises ThemeSegment_Checked
        }
    }

    static winrt::Windows::UI::Color ColorFromHex(winrt::hstring const& hex)
    {
        std::wstring s{ hex.c_str() };
        if (!s.empty() && s.front() == L'#')
        {
            s.erase(s.begin());
        }
        unsigned long v = wcstoul(s.c_str(), nullptr, 16);
        winrt::Windows::UI::Color c{};
        if (s.size() <= 6)
        {
            c.A = 255;
            c.R = static_cast<uint8_t>((v >> 16) & 0xFF);
            c.G = static_cast<uint8_t>((v >> 8) & 0xFF);
            c.B = static_cast<uint8_t>(v & 0xFF);
        }
        else
        {
            c.A = static_cast<uint8_t>((v >> 24) & 0xFF);
            c.R = static_cast<uint8_t>((v >> 16) & 0xFF);
            c.G = static_cast<uint8_t>((v >> 8) & 0xFF);
            c.B = static_cast<uint8_t>(v & 0xFF);
        }
        return c;
    }

    void MainWindow::ApplyAccentColor(winrt::hstring const& hex)
    {
        EnsureAccentBrushes();

        // Move the selection ring to the chosen swatch.
        winrt::Microsoft::UI::Xaml::Shapes::Ellipse sws[] = {
            AccentSw0(), AccentSw1(), AccentSw2(), AccentSw3(), AccentSw4()
        };
        std::wstring selected{ hex.c_str() };
        for (auto const& e : sws)
        {
            if (!e)
            {
                continue;
            }
            bool isSel = std::wstring{ ReadTagString(e.Tag()).c_str() } == selected;
            if (isSel && m_brushStroke)
            {
                e.Stroke(m_brushStroke);
            }
            e.StrokeThickness(isSel ? 2.0 : 0.0);
        }

        // The captured accent brushes ARE the shared theme-dictionary
        // SolidColorBrush instances; mutating their Color in place recolors
        // every {ThemeResource AccentBrush}/{AccentSoftBrush} consumer live
        // (nav highlights, slider foregrounds, accent glyphs/text, etc.).
        auto color = ColorFromHex(hex);
        if (auto ab = m_brushAccent.try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>())
        {
            ab.Color(color);
        }
        if (auto sb = m_brushAccentSoft.try_as<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush>())
        {
            auto soft = color;
            soft.A = sb.Color().A; // keep the soft brush's translucency
            sb.Color(soft);
        }
    }

    void MainWindow::EnsureTrayIcon()
    {
        if (m_trayIcon && m_trayIcon->IsActive())
        {
            return;
        }
        if (!m_trayIcon)
        {
            m_trayIcon = std::make_shared<LastMusicPlayer::Backend::TrayIcon>();
        }

        HICON icon = reinterpret_cast<HICON>(LoadImageW(
            nullptr,
            GetAppAssetPath(L"Assets\\AppIcon.ico").c_str(),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_LOADFROMFILE));

        auto weak = get_weak();
        m_trayIcon->OnRestore = [weak]()
        {
            if (auto self = weak.get())
            {
                if (self->m_appWindow)
                {
                    self->m_appWindow.Show();
                }
                if (self->m_hwnd)
                {
                    ShowWindow(self->m_hwnd, SW_SHOW);
                    SetForegroundWindow(self->m_hwnd);
                }
                if (self->m_trayIcon)
                {
                    self->m_trayIcon->Remove();
                }
            }
        };
        m_trayIcon->OnExit = [weak]()
        {
            if (auto self = weak.get())
            {
                self->m_forceExit = true;
                self->m_cast.Disconnect();
                self->ClearCastCallbacks();
                if (self->m_trayIcon)
                {
                    self->m_trayIcon->Remove();
                }
                if (auto app = winrt::Microsoft::UI::Xaml::Application::Current())
                {
                    app.Exit();
                }
            }
        };
        m_trayIcon->Create(icon, L"Last Music Player");
    }


    winrt::Windows::Foundation::IAsyncAction MainWindow::PopulateOutputDevicesAsync()
    {
        auto lifetime = get_strong();
        auto savedId = SettingsManagerService().GetString(L"OutputDeviceId", L"");

        std::vector<std::pair<winrt::hstring, winrt::hstring>> devices; // id, name
        try
        {
            auto selector = winrt::Windows::Media::Devices::MediaDevice::GetAudioRenderSelector();
            auto found = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(selector);
            for (auto const& d : found)
            {
                devices.emplace_back(d.Id(), d.Name());
            }
        }
        catch (...)
        {
        }

        auto combo = OutputDeviceCombo();
        if (!combo)
        {
            co_return;
        }

        m_loadingSettings = true;
        combo.Items().Clear();
        m_outputDeviceIds.clear();

        combo.Items().Append(winrt::box_value(winrt::hstring{ L"System default" }));
        m_outputDeviceIds.push_back(L""); // index 0 == system default

        int selectedIndex = 0;
        for (auto const& [id, deviceName] : devices)
        {
            combo.Items().Append(winrt::box_value(deviceName));
            m_outputDeviceIds.push_back(id);
            if (!savedId.empty() && id == savedId)
            {
                selectedIndex = static_cast<int>(m_outputDeviceIds.size()) - 1;
            }
        }
        combo.SelectedIndex(selectedIndex);
        m_loadingSettings = false;

        co_await ApplyOutputDeviceAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ApplyOutputDeviceAsync()
    {
        auto lifetime = get_strong();
        auto savedId = SettingsManagerService().GetString(L"OutputDeviceId", L"");
        try
        {
            auto mp = AudioPlayerService().GetMediaPlayer();
            if (savedId.empty())
            {
                mp.AudioDeviceType(winrt::Windows::Media::Playback::MediaPlayerAudioDeviceType::Multimedia);
            }
            else
            {
                auto info = co_await winrt::Windows::Devices::Enumeration::DeviceInformation::CreateFromIdAsync(savedId);
                if (info)
                {
                    mp.AudioDevice(info);
                }
            }
        }
        catch (...)
        {
        }
    }

    void MainWindow::ApplyDiscordPresence()
    {
        bool on = SettingsManagerService().GetBool(L"DiscordPresence", false);
        if (on)
        {
            if (!m_discord)
            {
                m_discord = std::make_shared<LastMusicPlayer::Backend::DiscordPresence>();
            }
            if (!m_discord->IsConnected())
            {
                m_discord->Connect();
            }
            auto current = AudioPlayerService().GetCurrentTrack();
            if (current)
            {
                UpdateDiscordNowPlaying(current);
            }
        }
        else if (m_discord)
        {
            m_discord->Clear();
        }
    }

    bool MainWindow::IsDiscordPlaybackActive(winrt::Windows::Media::Playback::MediaPlaybackState state)
    {
        using winrt::Windows::Media::Playback::MediaPlaybackState;
        return state == MediaPlaybackState::Playing
            || state == MediaPlaybackState::Opening
            || state == MediaPlaybackState::Buffering;
    }

    void MainWindow::SampleDiscordPlaybackSnapshot(bool& isPlaying, double& positionSeconds, double& durationSeconds)
    {
        isPlaying = true;
        positionSeconds = 0.0;
        durationSeconds = 0.0;

        if (m_sink == PlaybackSink::Cast)
        {
            isPlaying = m_castSession.IsPlaying;
            positionSeconds = m_castSession.CurrentSeconds;
            if (m_castSession.IsPlaying && m_castSession.ProgressStampMs > 0)
            {
                auto now = ::GetTickCount64();
                positionSeconds += static_cast<double>(now - m_castSession.ProgressStampMs) / 1000.0;
            }
            durationSeconds = m_castSession.DurationSeconds;
            if (durationSeconds <= 0.5)
            {
                auto current = AudioPlayerService().GetCurrentTrack();
                if (current && current.DurationSeconds() > 0.5)
                {
                    durationSeconds = current.DurationSeconds();
                }
            }
            return;
        }

        try
        {
            auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();
            auto state = session.PlaybackState();
            isPlaying = IsDiscordPlaybackActive(state);
            auto natural = static_cast<double>(session.NaturalDuration().count()) / 10000000.0;
            if (natural > 0.5)
            {
                durationSeconds = natural;
            }
            positionSeconds = static_cast<double>(session.Position().count()) / 10000000.0;
        }
        catch (...)
        {
        }
    }

    void MainWindow::UpdateDiscordPlaybackState(bool isPlaying, double positionSeconds, double durationSeconds)
    {
        if (!SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }
        auto current = AudioPlayerService().GetCurrentTrack();
        if (!current)
        {
            if (m_discord)
            {
                m_discord->Clear();
            }
            return;
        }
        if (!m_discord || !m_discord->IsConnected())
        {
            UpdateDiscordNowPlaying(current);
            return;
        }
        m_discord->SetPlaybackState(isPlaying, positionSeconds, durationSeconds);
        m_discordPresenceRefreshMs = ::GetTickCount64();
    }

    void MainWindow::RefreshDiscordPresenceIfNeeded(bool isPlaying, double positionSeconds, double durationSeconds)
    {
        if (!SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }

        auto now = ::GetTickCount64();
        bool connected = m_discord && m_discord->IsConnected();
        if (!connected)
        {
            if (m_discordReconnectAttemptMs != 0 && now - m_discordReconnectAttemptMs < 5000)
            {
                return;
            }
            m_discordReconnectAttemptMs = now;
        }
        else
        {
            if (m_discordPresenceRefreshMs != 0 && now - m_discordPresenceRefreshMs < 60000)
            {
                return;
            }
            m_discordPresenceRefreshMs = now;
        }

        try
        {
            UpdateDiscordPlaybackState(isPlaying, positionSeconds, durationSeconds);
        }
        catch (...)
        {
        }
    }

    void MainWindow::UpdateDiscordNowPlaying(winrt::Last_Music_Player::TrackInfo const& track)
    {
        if (!track || !SettingsManagerService().GetBool(L"DiscordPresence", false))
        {
            return;
        }
        if (!m_discord)
        {
            m_discord = std::make_shared<LastMusicPlayer::Backend::DiscordPresence>();
        }
        if (!m_discord->IsConnected())
        {
            m_discord->Connect();
        }

        LastMusicPlayer::Backend::PresencePayload payload;
        payload.title = std::wstring{ track.Title().c_str() };
        payload.artist = std::wstring{ track.Artist().c_str() };
        payload.album = std::wstring{ track.Album().c_str() };
        if (LastMusicPlayer::Backend::IsSafeRemoteUrl(
            track.ArtworkUrl(),
            LastMusicPlayer::Backend::RemoteUrlUse::Durable))
        {
            payload.artworkUrl = std::wstring{ track.ArtworkUrl().c_str() };
        }
        payload.durationSeconds = track.DurationSeconds();
        payload.isLocal = std::wstring{ track.SourceKind().c_str() } == L"local";

        double position = 0.0;
        double duration = payload.durationSeconds;
        bool playing = true;
        SampleDiscordPlaybackSnapshot(playing, position, duration);
        if (duration > 0.5)
        {
            payload.durationSeconds = duration;
        }
        payload.positionSeconds = position;
        payload.isPlaying = playing;

        m_discord->SetNowPlaying(payload);
        m_discordPresenceRefreshMs = ::GetTickCount64();

        if (!m_discord->IsConnected()
            || RemoteMusicServiceService().Mode() != LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            || payload.title.empty())
        {
            return;
        }

        ResolveDiscordArtworkAsync(
            winrt::hstring{ payload.title },
            winrt::hstring{ payload.artist });
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResolveDiscordArtworkAsync(
        winrt::hstring trackTitle, winrt::hstring trackArtist)
    {
        auto lifetime = get_strong();

        if (RemoteMusicServiceService().Mode() != LastMusicPlayer::Backend::RemoteAccessMode::ApiKey)
        {
            co_return;
        }
        auto savedBaseUrl = ReadAppSettingString(L"ProviderBaseUrl");
        auto savedApiKey = CredentialStoreService().ReadProviderApiKey();
        if (savedBaseUrl.empty() || trackTitle.empty() && trackArtist.empty())
        {
            co_return;
        }

        LastMusicPlayer::Backend::ProviderClient client;
        client.SetBaseUrl(savedBaseUrl);
        client.SetBearerToken(savedApiKey);

        winrt::hstring proxyUrl;
        try
        {
            proxyUrl = co_await client.ResolveDiscordArtworkAsync(trackTitle, trackArtist);
        }
        catch (...)
        {
            co_return;
        }

        if (proxyUrl.empty()
            || RemoteMusicServiceService().Mode()
                != LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            || ReadAppSettingString(L"ProviderBaseUrl") != savedBaseUrl
            || CredentialStoreService().ReadProviderApiKey() != savedApiKey
            || !LastMusicPlayer::Backend::IsSafeRemoteUrl(
                proxyUrl,
                LastMusicPlayer::Backend::RemoteUrlUse::Durable)
            || !m_discord
            || !m_discord->IsConnected())
        {
            co_return;
        }
        m_discord->SetArtworkProxyUrl(
            std::wstring{ proxyUrl.c_str() },
            std::wstring{ trackTitle.c_str() },
            std::wstring{ trackArtist.c_str() });
    }


    void MainWindow::UpdateAboutStats()
    {
        if (!AboutDbStats())
        {
            return;
        }
        try
        {
            if (DatabaseService().IsInitialized())
            {
                auto st = DatabaseService().GetLibraryStats();
                std::wstring text = L"SQLite 3 · "
                    + std::to_wstring(st.SongCount) + L" tracks · "
                    + std::to_wstring(st.AlbumCount) + L" albums · "
                    + std::to_wstring(st.ArtistCount) + L" artists";
                AboutDbStats().Text(winrt::hstring{ text });
            }
        }
        catch (...)
        {
        }
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::ResetDefaults_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog confirm;
        confirm.Title(winrt::box_value(winrt::hstring{ L"Reset settings" }));
        confirm.Content(winrt::box_value(winrt::hstring{
            L"Restore all preferences to their defaults? Your music library and "
            L"data are not affected." }));
        confirm.PrimaryButtonText(L"Reset");
        confirm.CloseButtonText(L"Cancel");
        confirm.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        confirm.XamlRoot(this->Content().XamlRoot());

        auto result = co_await confirm.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto& s = SettingsManagerService();
        s.SetBool(L"ShowAlbumArt", true);
        s.SetBool(L"WindowsMediaControls", true);
        s.SetBool(L"Gapless", true);
        s.SetBool(L"Autoplay", true);
        s.SetBool(L"DiscordPresence", false);
        s.SetInt(L"CloseBehavior", 0);
        s.SetString(L"ScanFormats", L".mp3,.flac");
        s.SetString(L"Theme", L"System");
        s.SetString(L"AccentColor", L"#FF0097B2");
        s.SetString(L"OutputDeviceId", L"");
        for (int i = 0; i < 10; ++i)
        {
            wchar_t key[16];
            std::swprintf(key, 16, L"EqBand%d", i);
            s.SetDouble(winrt::hstring{ key }, 0.0);
        }
        s.SetDouble(L"EqPreamp", 0.0);

        LoadSettingsIntoUi();
        ApplyDiscordPresence();
        co_await ApplyOutputDeviceAsync();
    }

    winrt::Windows::Foundation::IAsyncAction MainWindow::WipeAllData_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        if (m_cleanupInProgress)
        {
            co_return;
        }

        auto lifetime = get_strong();
        auto dispatcher = DispatcherQueue();

        winrt::Microsoft::UI::Xaml::Controls::TextBlock body;
        body.Text(L"This clears the local library database, playback history, liked tracks, playlists, queues, settings, provider config and app caches. Music files on disk are not deleted.");
        body.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::Wrap);
        body.FontSize(13.0);

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog confirm;
        confirm.Title(winrt::box_value(winrt::hstring{ L"Clean up everything?" }));
        confirm.Content(body);
        confirm.PrimaryButtonText(L"Clean up");
        confirm.CloseButtonText(L"Cancel");
        confirm.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
        confirm.XamlRoot(this->Content().XamlRoot());

        auto result = co_await confirm.ShowAsync();
        if (result != winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary)
        {
            co_return;
        }

        auto setCleanupStatus = [this](winrt::hstring const& text, bool warning = false)
        {
            (void)warning;
            try
            {
                if (auto status = WipeAllDataStatusText())
                {
                    status.Text(text);
                    status.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
                }
            }
            catch (...)
            {
            }
        };
        m_cleanupInProgress = true;
        if (!UserDataOperationGateService().CloseAdmissions())
        {
            m_cleanupInProgress = false;
            setCleanupStatus(L"Another cleanup operation is already in progress.", true);
            co_return;
        }
        UserDataAdmissionGuard admissionGuard{ UserDataOperationGateService() };
        setCleanupStatus(L"Cleaning up app data...");
        if (auto button = WipeAllDataButton())
        {
            button.IsEnabled(false);
        }
        if (auto content = Content())
        {
            content.IsHitTestVisible(false);
        }

        auto finishCleanup = [this, &admissionGuard]()
        {
            admissionGuard.Reopen();
            m_cleanupInProgress = false;
            try
            {
                if (auto content = Content())
                {
                    content.IsHitTestVisible(true);
                }
            }
            catch (...)
            {
            }
            try
            {
                if (auto button = WipeAllDataButton())
                {
                    button.IsEnabled(true);
                }
            }
            catch (...)
            {
            }
        };

        if (m_libraryScan.InProgress)
        {
            m_libraryScan.CancelRequested = true;
            ++m_libraryScan.Epoch;
            SetLibraryScanUi(false, L"", false);
        }

        ++m_homeHydration.StartupEpoch;
        ++m_homeHydration.HomeEpoch;
        ++m_homeHydration.MixRefreshId;
        ++m_songsHydrationEpoch;
        ++m_libraryHydrationEpoch;
        ++m_libraryDetailHydrationEpoch;
        ++m_searchDebounceId;
        ++m_searchRequestId;
        ++m_nowPlayingArtworkEpoch;
        ++m_lyricsHydrationEpoch;
        ++m_autoplay.Epoch;
        ++m_discoverEpoch;
        ++m_remotePlaybackResolveEpoch;
        m_homeHydration.InFlight = false;
        m_homeHydration.Pending = false;
        m_homeHydration.PendingRefresh = false;
        m_autoplay.InFlight = false;
        m_autoplay.ResumeWhenReady = false;
        m_autoplay.SeenKeys.clear();
        m_remoteSearchCache.clear();
        m_playbackHistoryQualifier.Clear();
        m_pendingPlaybackTrack = nullptr;
        m_pendingPlaybackIdentity.clear();
        m_pendingPlaybackEventId.clear();
        m_pendingPlaybackOwnerId.clear();
        m_pendingPlaybackRemoteId.clear();
        m_streamRecoverAttempts = 0;
        m_lastStreamRecoverTickMs = 0;
        m_pendingResumeSeekSeconds = -1.0;

        if (m_lyricsHydrationTimer)
        {
            m_lyricsHydrationTimer.Stop();
        }
        if (m_volumePersistTimer)
        {
            m_volumePersistTimer.Stop();
        }
        m_volumePersistQueued = false;
        if (m_playbackNoticeTimer)
        {
            m_playbackNoticeTimer.Stop();
        }
        if (PlaybackNoticePanel())
        {
            PlaybackNoticePanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        try
        {
            m_cast.Stop();
            m_cast.Disconnect();
            ClearCastCallbacks();
        }
        catch (...)
        {
        }
        m_sink = PlaybackSink::Local;
        m_castSession = {};
        EnsureAccentBrushes();
        if (auto icon = CastIcon())
        {
            icon.Foreground(m_brushGlyphIdle);
        }

        try
        {
            AudioPlayerService().ClearTrack();
        }
        catch (...)
        {
        }
        if (m_discord)
        {
            m_discord->Clear();
        }
        try
        {
            if (m_windowSmtc)
            {
                m_windowSmtc.PlaybackStatus(winrt::Windows::Media::MediaPlaybackStatus::Stopped);
                auto updater = m_windowSmtc.DisplayUpdater();
                updater.MusicProperties().Title(L"");
                updater.MusicProperties().Artist(L"");
                updater.Thumbnail(nullptr);
                updater.Update();
            }
        }
        catch (...)
        {
        }

        AccountSessionService().CancelSignIn();
        StreamCacheService().InvalidateInFlight();

        bool accountSessionCleared{};
        bool providerCredentialCleared{};
        bool dbCleared = true;
        bool cacheCleared = true;
        bool appStateCleared = true;
        bool settingsCleared = true;

        co_await winrt::resume_background();
        if (!UserDataOperationGateService().WaitForIdle(std::chrono::seconds(30)))
        {
            co_await wil::resume_foreground(dispatcher);
            finishCleanup();
            setCleanupStatus(
                L"Cleanup stopped because a background operation did not finish. Try again after it completes.",
                true);
            co_return;
        }

        accountSessionCleared = AccountSessionService().RemoveLocalSession();
        providerCredentialCleared = CredentialStoreService().DeleteProviderApiKey();
        try
        {
            settingsCleared = RemoteMusicServiceService().SetMode(
                LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
            DatabaseService().SetRemoteLibraryContext(L"LocalOnly");
        }
        catch (...)
        {
            settingsCleared = false;
        }

        try
        {
            appStateCleared = ClearSavedAppState();
        }
        catch (...)
        {
            appStateCleared = false;
        }

        try
        {
            if (!DatabaseService().IsInitialized())
            {
                dbCleared = DatabaseService().Initialize();
            }
            if (dbCleared)
            {
                dbCleared = DatabaseService().ClearAllUserData();
            }
        }
        catch (...)
        {
            dbCleared = false;
        }

        try
        {
            auto streamCacheCleared = StreamCacheService().Clear();
            std::error_code ec;
            std::filesystem::remove_all(AppDataDirectory() / L"thumbs", ec);
            cacheCleared = streamCacheCleared && !ec;
        }
        catch (...)
        {
            cacheCleared = false;
        }

        co_await wil::resume_foreground(dispatcher);

        bool uiResetSucceeded = true;
        try
        {
            settingsCleared = SettingsManagerService().Reset() && settingsCleared;
            m_pendingResumeTrack = nullptr;
            m_homeRecentHistory.clear();
            m_homePlayCounts.clear();
            m_homeLastPlayedOrder.clear();
            m_homeMixes.clear();
            m_homeRankedGenres.clear();
            m_homeGenrePools.clear();
            m_homeMixGenres.clear();
            m_catalogTracks.clear();
            ClearAccountArtworkCache();
            m_discoverLoaded = false;
            m_discoverStorefront = {};
            m_catalogDiscovery = {};
            m_catalogContentStorefront = {};
            m_catalogBackStack.clear();
            m_catalogGalleryCharts.clear();
            m_catalogLikeOverrides.clear();
            m_discoverChartRequest = {};
            m_discoverChartNextOffset = 0;
            m_discoverChartHasMore = false;
            m_discoverChartItems.Clear();
            m_discoverDetailTracks.Clear();
            m_searchAllResults.clear();
            m_songsAllResults.clear();
            m_librarySongAllResults.clear();
            m_libraryDetailAllResults.clear();
            m_songsMatchedCount = 0;
            m_songsMatchedSeconds = 0.0;
            m_librarySongsMatchedCount = 0;
            m_librarySongsMatchedSeconds = 0.0;
            m_libraryDetailMatchedCount = 0;
            m_songsPageLoading = false;
            m_librarySongsPageLoading = false;
            m_libraryDetailPageLoading = false;
            ++m_songsPageLoadId;
            ++m_librarySongsPageLoadId;
            ++m_libraryDetailPageLoadId;
            m_libraryStats = {};
            m_catalogLoaded = true;
            m_songsResultsValid = true;
            m_homePlaySequence = 0;
            m_queue = {};
            m_libraryScope = L"All";

            m_homeTracks.Clear();
            m_recentlyAddedTracks.Clear();
            m_homeMostPlayedTracks.Clear();
            m_homeLikedTracks.Clear();
            m_songsTracks.Clear();
            m_searchTracks.Clear();
            m_browseCategories.Clear();
            m_browseLandingLoaded = false;
            ++m_browseLandingEpoch;
            m_librarySongs.Clear();
            m_libraryGenres.Clear();
            m_yourPlaylists.Clear();
            m_manualPlaylists.Clear();
            m_autoPlaylists.Clear();
            m_sidebarPlaylists.Clear();
            m_libraryDetailTracks.Clear();
            m_upNextQueue.Clear();
            m_albums.Clear();
            m_artists.Clear();
            HideLibraryDetail();
            RefreshAutoPlaylists();
            UpdateShuffleRepeatVisuals();
            UpdateSongsStats();
            UpdateAboutStats();
            UpdateLibraryActionButtons();

            if (auto box = MusicFolderPathBox()) box.Text(L"");
            if (auto box = ProviderBaseUrlBox()) box.Text(L"");
            if (auto box = ProviderApiKeyBox()) box.Password(L"");
            if (auto text = ProviderTestStatusText()) text.Text(L"Not configured");
            if (auto box = DisplayNameBox()) box.Text(L"");
            if (auto btn = DisplayNameSaveButton()) btn.IsEnabled(false);

            auto clearArtwork = [this]()
            {
                if (auto image = BottomPlayerArt()) image.Source(nullptr);
                if (auto image = RightPanelArt()) image.Source(nullptr);
                if (auto image = FsArt()) image.Source(nullptr);
                if (auto art = BottomPlayerArt()) art.Opacity(0.0);
                if (auto art = RightPanelArt()) art.Opacity(0.0);
                if (auto art = FsArt()) art.Opacity(0.0);
                if (auto generated = BottomGeneratedArtwork()) generated.Opacity(1.0);
                if (auto generated = RightPanelGeneratedArtwork()) generated.Opacity(1.0);
                if (auto generated = FsGeneratedArtwork()) generated.Opacity(1.0);
            };
            clearArtwork();
            if (auto title = BottomPlayerTitle()) title.Text(L"Not Playing");
            if (auto artist = BottomPlayerArtist()) artist.Text(L"Select a track");
            if (auto title = NowPlayingTitle()) title.Text(L"Not Playing");
            if (auto artist = NowPlayingArtist()) artist.Text(L"Select a track");
            if (auto title = FsTitle()) title.Text(L"Not Playing");
            if (auto artist = FsArtist()) artist.Text(L"Select a track");
            if (auto title = RightPanelGeneratedTitle()) title.Text(L"MUSIC");
            if (auto caption = RightPanelGeneratedCaption()) caption.Text(L"Select a track");
            if (auto glyph = BottomGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto glyph = RightPanelGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto glyph = FsGeneratedGlyph()) glyph.Glyph(L"\xE8D6");
            if (auto text = NpMetaAlbum()) text.Text(L"\x2014");
            if (auto text = NpMetaYear()) text.Text(L"\x2014");
            if (auto text = NpMetaFormat()) text.Text(L"\x2014");
            if (auto icon = PlayPauseIcon()) icon.Glyph(L"\xE768");
            if (auto icon = FsPlayPauseIcon()) icon.Glyph(L"\xE768");
            if (auto icon = BottomLikeIcon())
            {
                icon.Glyph(L"\xEB51");
                if (m_brushGlyphIdle) icon.Foreground(m_brushGlyphIdle);
            }
            if (auto icon = FsLikeIcon())
            {
                icon.Glyph(L"\xEB51");
                if (m_brushGlyphIdle) icon.Foreground(m_brushGlyphIdle);
            }
            if (auto button = BottomLikeButton())
            {
                button.Tag(winrt::box_value(winrt::hstring{}));
            }
            ApplyPlaybackProgress(0.0, 0.0);
            if (auto t = LyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = LyricsTrackArtist()) t.Text(L"Select a track");
            if (auto t = FsLyricsTrackTitle()) t.Text(L"Not Playing");
            if (auto t = FsLyricsTrackArtist()) t.Text(L"Select a track");
            ResetLyricsViewToEmpty(L"Tap Lyrics while a song is playing.");

            if (auto section = HomeRecentlyAddedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto section = HomeMostPlayedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto section = HomeLikedSection())
            {
                section.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto grid = HomeRecentGridView())
            {
                grid.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }
            if (auto empty = ListenAgainEmptyText())
            {
                empty.Text(L"Play something to see it here");
                empty.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }

            LoadSettingsIntoUi();
            RefreshAccountSettingsUi();
            UpdateCatalogAvailability();
            UpdateSongsScopeLabel();
            ApplyUserDisplayName();
            m_loadingSettings = true;
            try
            {
                if (auto combo = OutputDeviceCombo())
                {
                    combo.SelectedIndex(0);
                }
            }
            catch (...)
            {
            }
            m_loadingSettings = false;
            co_await ApplyOutputDeviceAsync();
        }
        catch (...)
        {
            settingsCleared = false;
            uiResetSucceeded = false;
        }

        finishCleanup();
        if (dbCleared && cacheCleared && appStateCleared && settingsCleared
            && accountSessionCleared && providerCredentialCleared
            && uiResetSucceeded)
        {
            setCleanupStatus(L"Cleanup complete. Music files on disk were left untouched.");
        }
        else if (!accountSessionCleared || !providerCredentialCleared)
        {
            setCleanupStatus(L"UI reset, but one secure credential could not be removed.", true);
        }
        else if (!dbCleared)
        {
            setCleanupStatus(L"UI reset, but the database could not be cleared. Close the app and try again.", true);
        }
        else if (!cacheCleared)
        {
            setCleanupStatus(L"Cleanup finished, but one cache folder could not be fully removed.", true);
        }
        else if (!appStateCleared)
        {
            setCleanupStatus(L"Cleanup finished, but the saved playback state could not be removed.", true);
        }
        else if (!settingsCleared)
        {
            setCleanupStatus(L"Cleanup finished, but the settings files could not be fully reset.", true);
        }
        else
        {
            setCleanupStatus(L"Cleanup finished, but part of the visible UI could not be refreshed.", true);
        }
    }

    // Pushes persisted settings into the Settings UI controls. Runs with
    // m_loadingSettings set so each control's change handler no-ops during
    // the initial sync.
    void MainWindow::LoadSettingsIntoUi()
    {
        m_loadingSettings = true;
        auto& s = SettingsManagerService();

        if (ShowAlbumArtSwitch())         ShowAlbumArtSwitch().IsOn(s.GetBool(L"ShowAlbumArt", true));
        if (WindowsMediaControlsSwitch()) WindowsMediaControlsSwitch().IsOn(s.GetBool(L"WindowsMediaControls", true));
        if (GaplessSwitch())              GaplessSwitch().IsOn(s.GetBool(L"Gapless", true));
        if (AutoplaySwitch())             AutoplaySwitch().IsOn(s.GetBool(L"Autoplay", true));
        if (DiscordPresenceSwitch())      DiscordPresenceSwitch().IsOn(s.GetBool(L"DiscordPresence", false));
        if (AutoSyncSwitch())             AutoSyncSwitch().IsOn(s.GetBool(L"AutoSyncAccount", true));

        if (CloseBehaviorCombo())         CloseBehaviorCombo().SelectedIndex(s.GetInt(L"CloseBehavior", 0));

        {
            std::unordered_set<std::wstring> exts;
            for (auto const& ext : ScanFileExtensions())
            {
                exts.insert(ToLowerCopy(ext));
            }
            auto has = [&](wchar_t const* e) { return exts.find(e) != exts.end(); };
            if (FmtMp3())  FmtMp3().IsChecked(has(L".mp3"));
            if (FmtFlac()) FmtFlac().IsChecked(has(L".flac"));
            if (FmtWav())  FmtWav().IsChecked(has(L".wav"));
            if (FmtM4a())  FmtM4a().IsChecked(has(L".m4a"));
            if (FmtAac())  FmtAac().IsChecked(has(L".aac"));
            if (FmtOgg())  FmtOgg().IsChecked(has(L".ogg"));
            if (FmtOpus()) FmtOpus().IsChecked(has(L".opus"));
            if (FmtWma())  FmtWma().IsChecked(has(L".wma"));
        }

        ApplyShowAlbumArt();
        ApplyWindowsMediaControls();
        ApplyThemeFromSetting();
        ApplyAccentColor(s.GetString(L"AccentColor", L"#FF0097B2"));

        m_loadingSettings = false;
    }

    void MainWindow::BuildEqualizerBars()
    {
        const wchar_t* freqs[] = { L"32", L"64", L"125", L"250", L"500", L"1k", L"2k", L"4k", L"8k", L"16k" };

        auto host = EqualizerBars();
        host.Children().Clear();
        m_equalizerSliders.clear();

        // Defer the EQ attach until the user actually drags a band. Calling
        // AddAudioEffect at startup added activation overhead and risked
        // perturbing MediaPlayer before its Source was set; lazy-attach via
        // UpdateEqualizerBand keeps the cold path identical to pre-EQ.

        for (int i = 0; i < 10; ++i)
        {
            winrt::Microsoft::UI::Xaml::Controls::StackPanel column;
            column.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Vertical);
            column.Spacing(8.0);
            column.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);

            winrt::Microsoft::UI::Xaml::Controls::Slider bar;
            bar.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Vertical);
            bar.Minimum(-12.0);
            bar.Maximum(12.0);
            bar.Value(0.0);
            bar.StepFrequency(1.0);
            bar.Height(132.0);
            bar.IsEnabled(false);
            bar.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);

            int bandIndex = i;
            bar.ValueChanged([this, bandIndex](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
            {
                (void)sender;
                double value = args.NewValue();
                AudioPlayerService().UpdateEqualizerBand(bandIndex, value);
                if (!m_loadingSettings)
                {
                    wchar_t key[16];
                    std::swprintf(key, 16, L"EqBand%d", bandIndex);
                    SettingsManagerService().SetDouble(winrt::hstring{ key }, value);
                    // Re-check which preset chip should be highlighted now
                    // that the user has nudged a band. Suppressed while a
                    // preset click is programmatically driving the sliders
                    // (m_skipPresetSync), so partial sequences of band
                    // updates do not flicker the chip selection.
                    SyncActiveEqualizerPreset();
                }
            });

            winrt::Microsoft::UI::Xaml::Controls::TextBlock lbl;
            lbl.Text(winrt::hstring(freqs[i]));
            lbl.FontSize(11.0);
            lbl.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Center);
            EnsureAccentBrushes();
            if (m_brushGlyphIdle)
            {
                lbl.Foreground(m_brushGlyphIdle);
            }

            column.Children().Append(bar);
            column.Children().Append(lbl);
            host.Children().Append(column);
            m_equalizerSliders.push_back(bar);
        }
    }

    void MainWindow::ApplyEqualizerFromSettings()
    {
        m_loadingSettings = true;
        double gains[10]{};
        for (int i = 0; i < 10; ++i)
        {
            wchar_t key[16];
            std::swprintf(key, 16, L"EqBand%d", i);
            gains[i] = SettingsManagerService().GetDouble(winrt::hstring{ key }, 0.0);
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                m_equalizerSliders[i].Value(gains[i]);
            }
        }
        AudioPlayerService().ApplyEqualizerBands(gains);

        // Preamp (slider range is [-12, 0]). The slider ValueChanged also
        // pushes the value to AudioPlayer, but call SetEqualizerPreamp
        // explicitly so the effect picks the value up even if the slider
        // didn't move (Slider.Value(x) on a slider already at x is a no-op).
        double preamp = SettingsManagerService().GetDouble(L"EqPreamp", 0.0);
        if (preamp < -12.0) preamp = -12.0;
        if (preamp > 0.0) preamp = 0.0;
        if (auto sl = EqPreampSlider())
        {
            sl.Value(preamp);
        }
        AudioPlayerService().SetEqualizerPreamp(preamp);

        // Attach the effect even when every band is at 0 dB. The effect
        // short-circuits in passthrough mode, so the cost is negligible,
        // but having it in the chain from startup means slider drags never
        // trigger a mid-playback graph reconfigure.
        AudioPlayerService().EnsureEqualizerAttached();
        m_loadingSettings = false;
        SyncActiveEqualizerPreset();
    }

    void MainWindow::ApplyEqualizerPreset(std::array<double, 10> const& gains)
    {
        // Drive the sliders; ValueChanged persists each band and pushes it to
        // the effect's PropertySet. m_skipPresetSync is set by the caller so
        // intermediate band states do not flicker the chip selection.
        for (int i = 0; i < 10; ++i)
        {
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                m_equalizerSliders[i].Value(gains[static_cast<size_t>(i)]);
            }
        }
    }

    namespace
    {
        // Canonical gain shapes for the named presets. The detection routine
        // below picks the matching preset name; mismatches fall through to
        // "Custom". Keep in sync with EqualizerPreset_Click's gain arrays.
        struct PresetShape
        {
            wchar_t const* Tag;
            std::array<double, 10> Gains;
        };

        wchar_t const* DetectActivePresetForGains(std::array<double, 10> const& gains)
        {
            static const PresetShape shapes[] = {
                { L"Flat",       { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
                { L"Bass",       { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 } },
                { L"Treble",     { 0, 0, 0, 0, 0, 1, 3, 5, 6, 6 } },
                { L"Vocal",      { -2, -1, 0, 1, 3, 4, 4, 2, 0, -1 } },
                { L"Acoustic",   { 2, 3, 2, 1, 1, 2, 3, 4, 3, 2 } },
                { L"Electronic", { 4, 4, 2, 0, -2, 0, 1, 3, 4, 5 } },
            };
            auto matches = [&](std::array<double, 10> const& p)
            {
                for (size_t i = 0; i < 10; ++i)
                {
                    // EQ step is 1 dB; an epsilon below 0.5 keeps detection
                    // exact for integer presets while tolerating float drift
                    // from persisted/restored values.
                    if (std::abs(gains[i] - p[i]) > 0.25) return false;
                }
                return true;
            };
            for (auto const& s : shapes)
            {
                if (matches(s.Gains)) return s.Tag;
            }
            return L"Custom";
        }
    }

    void MainWindow::SyncActiveEqualizerPreset()
    {
        // Callers (notably ApplyEqualizerPreset's slider drives) set
        // m_skipPresetSync so intermediate per-band states do not flicker
        // the chip selection through "Custom" while a preset is being
        // applied one band at a time.
        if (m_skipPresetSync) return;

        std::array<double, 10> gains{};
        for (int i = 0; i < 10; ++i)
        {
            if (i < static_cast<int>(m_equalizerSliders.size()) && m_equalizerSliders[i])
            {
                gains[static_cast<size_t>(i)] = m_equalizerSliders[i].Value();
            }
        }
        auto active = DetectActivePresetForGains(gains);

        struct Chip
        {
            winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton Btn;
            wchar_t const* Tag;
        };
        Chip chips[] = {
            { EqPresetFlat(),       L"Flat" },
            { EqPresetBass(),       L"Bass" },
            { EqPresetTreble(),     L"Treble" },
            { EqPresetVocal(),      L"Vocal" },
            { EqPresetAcoustic(),   L"Acoustic" },
            { EqPresetElectronic(), L"Electronic" },
            { EqPresetCustom(),     L"Custom" },
        };
        // Chips fire Click only on user input, not on programmatic
        // IsChecked changes — no recursion guard needed here.
        for (auto const& c : chips)
        {
            if (!c.Btn) continue;
            bool shouldCheck = (std::wcscmp(active, c.Tag) == 0);
            c.Btn.IsChecked(shouldCheck);
        }
    }

    void MainWindow::EqualizerPreset_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)args;
        auto btn = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton>();
        if (!btn) return;

        // ToggleButton.Click fires after the framework has flipped IsChecked.
        // Force it back to checked so clicking an already-active chip does
        // not un-highlight it (chips model exclusive selection, not toggle).
        btn.IsChecked(true);

        // Uncheck the other preset chips so only one stays "active".
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton presets[] = {
            EqPresetFlat(), EqPresetBass(), EqPresetTreble(), EqPresetVocal(),
            EqPresetAcoustic(), EqPresetElectronic(), EqPresetCustom()
        };
        for (auto const& p : presets)
        {
            if (p && p != btn)
            {
                p.IsChecked(false);
            }
        }

        auto tag = ReadTagString(btn.Tag());
        if (tag == L"Custom")
        {
            // Custom is IsHitTestVisible=False; user clicks can't reach here.
            // Kept as defensive no-op in case the chip is enabled later.
            return;
        }

        // Bands are 32, 64, 125, 250, 500, 1k, 2k, 4k, 8k, 16k Hz. Presets
        // are illustrative shapes — feel free to tune later.
        std::array<double, 10> gains{};
        if (tag == L"Bass") gains = { 6, 5, 4, 2, 0, 0, 0, 0, 0, 0 };
        else if (tag == L"Treble") gains = { 0, 0, 0, 0, 0, 1, 3, 5, 6, 6 };
        else if (tag == L"Vocal") gains = { -2, -1, 0, 1, 3, 4, 4, 2, 0, -1 };
        else if (tag == L"Acoustic") gains = { 2, 3, 2, 1, 1, 2, 3, 4, 3, 2 };
        else if (tag == L"Electronic") gains = { 4, 4, 2, 0, -2, 0, 1, 3, 4, 5 };
        // "Flat" (and anything else) → all zeros.

        // Drive sliders under skip-sync so partial intermediate band states
        // do not bounce the chip selection mid-apply through "Custom".
        m_skipPresetSync = true;
        ApplyEqualizerPreset(gains);
        m_skipPresetSync = false;
    }

    void MainWindow::EqResetButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;

        // Zero every band (ValueChanged persists each EqBandN).
        std::array<double, 10> zeros{};
        m_skipPresetSync = true;
        ApplyEqualizerPreset(zeros);
        m_skipPresetSync = false;

        // Zero the preamp (ValueChanged persists EqPreamp).
        if (auto sl = EqPreampSlider())
        {
            sl.Value(0.0);
        }

        // Land on Flat now that everything is zeroed.
        SyncActiveEqualizerPreset();
    }

    void MainWindow::EqPreampSlider_ValueChanged(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args)
    {
        (void)sender;
        double value = args.NewValue();
        AudioPlayerService().SetEqualizerPreamp(value);

        if (auto text = EqPreampValueText())
        {
            wchar_t buf[16]{};
            if (std::abs(value) < 0.0001)
            {
                std::swprintf(buf, 16, L"0 dB");
            }
            else
            {
                // Slider clamps to [-12, 0]; format with one decimal if
                // needed but most user values land on integer steps.
                std::swprintf(buf, 16, L"%g dB", value);
            }
            text.Text(winrt::hstring{ buf });
        }

        if (!m_loadingSettings)
        {
            SettingsManagerService().SetDouble(L"EqPreamp", value);
        }
    }

}
