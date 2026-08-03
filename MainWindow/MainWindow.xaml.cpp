#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindow.Internal.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <windows.h>
#include <microsoft.ui.xaml.window.h>
#include <systemmediatransportcontrolsinterop.h>

// Backend
#include "Backend/AudioPlayer.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/ProviderClient.h"
#include "Backend/ProviderHelpers.h"
#include "Backend/SettingsManager.h"
#include "Backend/TrayIcon.h"
#include "Backend/DiscordPresence.h"

// Frontend
#include "Frontend/NavigationService.h"
#include "Frontend/UIHelpers.h"

#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.FileProperties.h>
#include <winrt/Windows.Storage.Search.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Media.Devices.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Microsoft.UI.Interop.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Text.h>
#include <string>
#include <memory>
#include <mutex>
#include <shobjidl.h> // For IInitializeWithWindow
#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <array>
#include <cwctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Last_Music_Player::implementation
{
    using namespace detail;

    namespace
    {
        std::atomic<uint64_t> g_appStateSaveVersion{ 0 };
        std::mutex g_appStateFileMutex;

        winrt::fire_and_forget WriteLatestAppStateAsync(
            winrt::hstring payload,
            uint64_t version,
            LastMusicPlayer::Backend::UserDataOperationGate::Lease lease)
        {
            (void)lease;
            co_await winrt::resume_background();

            try
            {
                std::lock_guard lock{ g_appStateFileMutex };
                if (g_appStateSaveVersion.load(std::memory_order_acquire) == version)
                {
                    WriteTextFile(StateFilePath(), payload);
                }
            }
            catch (...)
            {
            }
        }
    }

    MainWindow::MainWindow()
    {
        try
        {
            InitializeComponent();
        }
        catch (winrt::hresult_error const&)
        {
            throw;
        }
        catch (...)
        {
            throw;
        }
        m_xamlReadyForEvents = true;
        HomeRecentGridView().ItemsSource(m_homeTracks);
        HomeMostPlayedGridView().ItemsSource(m_homeMostPlayedTracks);
        HomeLikedGridView().ItemsSource(m_homeLikedTracks);
        MusicListView().ItemsSource(m_songsTracks);
        SongsGridView().ItemsSource(m_songsTracks);
        SearchSongsListView().ItemsSource(m_searchTracks);
        UpNextListView().ItemsSource(m_upNextQueue);
        FsUpNext().ItemsSource(m_upNextQueue);
        SidebarPlaylistsListView().ItemsSource(m_sidebarPlaylists);
        LibrarySongsListView().ItemsSource(m_librarySongs);
        LibAlbumsGrid().ItemsSource(m_albums);
        LibArtistsGrid().ItemsSource(m_artists);
        LibGenresGrid().ItemsSource(m_libraryGenres);
        LibManualPlaylistsGrid().ItemsSource(m_manualPlaylists);
        LibAutoPlaylistsGrid().ItemsSource(m_autoPlaylists);
        LibraryDetailTracksListView().ItemsSource(m_libraryDetailTracks);
        if (LibTabPlaylists())
        {
            LibTabPlaylists().IsChecked(true);
            LibraryTab_Checked(LibTabPlaylists(), nullptr);
        }
        BuildEqualizerBars();
        LoadSettingsIntoUi();
        RestorePlaybackPreferences();

        // When the resolved theme actually changes (system theme load, or the
        // user toggling Light/Dark), the captured nav/accent brushes go stale.
        // Re-capture and re-apply once resources have settled (deferred).
        if (auto themeRoot = this->Content().try_as<winrt::Microsoft::UI::Xaml::FrameworkElement>())
        {
            themeRoot.ActualThemeChanged([this](winrt::Microsoft::UI::Xaml::FrameworkElement const&, winrt::Windows::Foundation::IInspectable const&)
            {
                auto dq = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
                dq.TryEnqueue([this]()
                {
                    m_accentBrushesCaptured = false;
                    EnsureAccentBrushes();
                    UpdateNavSelection(m_currentNav);
                    UpdateSettingsSection(m_selectedSettingsSection);
                    ApplyAccentColor(SettingsManagerService().GetString(L"AccentColor", L"#FF0097B2"));
                });
            });
        }

        // Hide default title bar, extend content, and use our custom drag region.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(AppTitleBar());

        HWND hwnd{};
        if (auto windowNative = this->try_as<::IWindowNative>())
        {
            winrt::check_hresult(windowNative->get_WindowHandle(&hwnd));
        }

        if (hwnd != nullptr)
        {
            auto windowId = winrt::Microsoft::UI::GetWindowIdFromWindow(hwnd);
            auto appWindow = winrt::Microsoft::UI::Windowing::AppWindow::GetFromWindowId(windowId);
            m_hwnd = hwnd;
            m_appWindow = appWindow;
            appWindow.Closing({ this, &MainWindow::OnAppWindowClosing });
            auto const iconPath = GetAppAssetPath(L"Assets\\AppIcon.ico");
            appWindow.SetIcon(iconPath);

            static HICON s_smallIcon = nullptr;
            static HICON s_largeIcon = nullptr;
            if (!s_smallIcon)
            {
                s_smallIcon = reinterpret_cast<HICON>(LoadImageW(
                    nullptr,
                    iconPath.c_str(),
                    IMAGE_ICON,
                    GetSystemMetrics(SM_CXSMICON),
                    GetSystemMetrics(SM_CYSMICON),
                    LR_LOADFROMFILE));
            }
            if (!s_largeIcon)
            {
                s_largeIcon = reinterpret_cast<HICON>(LoadImageW(
                    nullptr,
                    iconPath.c_str(),
                    IMAGE_ICON,
                    GetSystemMetrics(SM_CXICON),
                    GetSystemMetrics(SM_CYICON),
                    LR_LOADFROMFILE));
            }
            if (s_smallIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(s_smallIcon));
            }
            if (s_largeIcon)
            {
                SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(s_largeIcon));
            }

            InstallMinimumWindowSize(hwnd);

            // Restore persisted window geometry if any. Settings load
            // happens just below in the next block, so we need the
            // manager loaded already — it loads lazily on first
            // GetInt/GetString, so the calls here trigger the load
            // for us. Sanity-check: refuse to apply geometry smaller
            // than the safe minimum or positions that would land the
            // window entirely off-screen.
            auto& settings = SettingsManagerService();
            int savedW = settings.GetInt(L"WindowWidth", 0);
            int savedH = settings.GetInt(L"WindowHeight", 0);
            int savedX = settings.GetInt(L"WindowX", INT_MIN);
            int savedY = settings.GetInt(L"WindowY", INT_MIN);
            bool haveSize = savedW >= 800 && savedH >= 600;
            bool havePos  = savedX != INT_MIN && savedY != INT_MIN;
            int finalW = haveSize ? savedW : kDefaultWindowWidth;
            int finalH = haveSize ? savedH : kDefaultWindowHeight;
            UINT flags = SWP_NOZORDER;
            int finalX = 0;
            int finalY = 0;
            if (havePos)
            {
                // Verify the saved position still intersects a real
                // monitor. If the user disconnected the monitor it
                // was on, fall back to whatever Windows picks.
                RECT probe{ savedX, savedY, savedX + finalW, savedY + finalH };
                HMONITOR mon = MonitorFromRect(&probe, MONITOR_DEFAULTTONULL);
                if (mon != nullptr)
                {
                    finalX = savedX;
                    finalY = savedY;
                }
                else
                {
                    flags |= SWP_NOMOVE;
                }
            }
            else
            {
                flags |= SWP_NOMOVE;
            }
            SetWindowPos(hwnd, NULL, finalX, finalY, finalW, finalH, flags);
        }

        // Load settings
        SettingsManagerService().Load();
        AudioPlayerService().SetVolume(SettingsManagerService().GetVolume());
        auto& m_audioPlayer = AudioPlayerService();

        m_audioPlayer.GetMediaPlayer().MediaEnded({ this, &MainWindow::OnMediaEnded });
        // Auto-recover a dropped/failed live stream, and apply the resume-seek
        // once the re-opened media is ready.
        m_audioPlayer.GetMediaPlayer().MediaFailed({ this, &MainWindow::OnMediaFailed });
        m_audioPlayer.GetMediaPlayer().MediaOpened({ this, &MainWindow::OnMediaOpened });
        m_audioPlayer.GetPlaybackList().CurrentItemChanged({ this, &MainWindow::OnPlaybackListCurrentItemChanged });
        ApplyGaplessSetting();

        // Wire Media Player events to the UI
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto session = AudioPlayerService().GetMediaPlayer().PlaybackSession();

        // 1. Position Changed (Background Thread) -> Update Slider and Current Time Text (UI Thread)
        session.PositionChanged([this, dispatcher](winrt::Windows::Media::Playback::MediaPlaybackSession const& sender, winrt::Windows::Foundation::IInspectable const&)
        {
            (void)sender;
            dispatcher.TryEnqueue([this]()
            {
                RefreshPlaybackProgress();
            });
        });

        // 2. Duration Changed (Background Thread) -> Update Slider Max and Total Time Text (UI Thread)
        session.NaturalDurationChanged([this, dispatcher](winrt::Windows::Media::Playback::MediaPlaybackSession const& sender, winrt::Windows::Foundation::IInspectable const&)
        {
            // Sample on the audio thread so the duration reflects what
            // the session sees right now (not after a UI dispatch hop).
            double duration = static_cast<double>(sender.NaturalDuration().count()) / 10000000.0;
            dispatcher.TryEnqueue([this, duration]()
            {
                RefreshPlaybackProgress();
                if (m_sink != PlaybackSink::Local) return;
                if (SettingsManagerService().GetBool(L"DiscordPresence", false))
                {
                    // Discord's progress bar only renders when the
                    // activity carries non-zero duration. UpdateDiscord
                    // NowPlaying runs while the media is still Opening
                    // and gets 0 for NaturalDuration; this catches the
                    // late update and triggers a re-send with timestamps.
                    bool playing = true;
                    double position = 0.0;
                    double snapshotDuration = duration;
                    SampleDiscordPlaybackSnapshot(playing, position, snapshotDuration);
                    if (snapshotDuration <= 0.5)
                    {
                        snapshotDuration = duration;
                    }
                    UpdateDiscordPlaybackState(playing, position, snapshotDuration);
                }
            });
        });

        // 3. Playback State Changed (Background Thread) -> Update Play/Pause icon (UI Thread)
        session.PlaybackStateChanged([this, dispatcher](winrt::Windows::Media::Playback::MediaPlaybackSession const& sender, winrt::Windows::Foundation::IInspectable const&)
        {
            auto state = sender.PlaybackState();
            // Snapshot position + duration on the audio thread before
            // marshalling — a paused-then-resumed track would otherwise
            // report position 0 if we read it on the UI dispatcher mid-
            // transition, and NaturalDuration is finally populated by the
            // time we reach a Playing state.
            double position = static_cast<double>(sender.Position().count()) / 10000000.0;
            double duration = static_cast<double>(sender.NaturalDuration().count()) / 10000000.0;
            dispatcher.TryEnqueue([this, state, position, duration]()
            {
                if (m_sink != PlaybackSink::Local) return;
                bool playing = IsDiscordPlaybackActive(state);
                auto glyph = playing
                    ? L"\xE769"  // Pause icon
                    : L"\xE768"; // Play icon
                PlayPauseIcon().Glyph(glyph);
                if (FsPlayPauseIcon()) FsPlayPauseIcon().Glyph(glyph);

                // Mirror playback state to the window-bound SMTC so the
                // OS treats our session as the active media app and
                // routes hardware media keys here (otherwise the session
                // looks Stopped and the keys go to another player).
                if (m_windowSmtc)
                {
                    using winrt::Windows::Media::MediaPlaybackStatus;
                    MediaPlaybackStatus s = MediaPlaybackStatus::Stopped;
                    switch (state)
                    {
                    case winrt::Windows::Media::Playback::MediaPlaybackState::Playing:
                        s = MediaPlaybackStatus::Playing; break;
                    case winrt::Windows::Media::Playback::MediaPlaybackState::Paused:
                        s = MediaPlaybackStatus::Paused; break;
                    case winrt::Windows::Media::Playback::MediaPlaybackState::Opening:
                    case winrt::Windows::Media::Playback::MediaPlaybackState::Buffering:
                        s = MediaPlaybackStatus::Changing; break;
                    default:
                        s = MediaPlaybackStatus::Stopped; break;
                    }
                    try { m_windowSmtc.PlaybackStatus(s); } catch (...) {}
                }

                UpdateDiscordPlaybackState(playing, position, duration);
            });
        });

        // MediaPlaybackSession.PositionChanged is not a reliable continuous UI
        // ticker for streamed media. Poll the live session on the UI thread so
        // the bottom and full-screen progress bars move while audio plays.
        m_playbackProgressTimer = winrt::Microsoft::UI::Xaml::DispatcherTimer();
        m_playbackProgressTimer.Interval(std::chrono::milliseconds(500));
        m_playbackProgressTimer.Tick([this](winrt::Windows::Foundation::IInspectable const&, winrt::Windows::Foundation::IInspectable const&)
        {
            RefreshPlaybackProgress();
        });
        m_playbackProgressTimer.Start();

        // Initialize SMTC (System Media Transport Controls).
        //
        // For an unpackaged Win32 + WinAppSDK app, MediaPlayer.SystemMedia
        // TransportControls returns a *player-internal* SMTC that doesn't
        // propagate to the OS's global media-session list (Win+V shows the
        // app as "Unknown app" with the .exe path, and media keys on the
        // keyboard never reach our ButtonPressed handler).
        //
        // The window-bound SMTC obtained via ISystemMediaTransportControls
        // Interop::GetForWindow is the one the OS routes keyboard media
        // keys to. We initialise THAT instance with the same enable flags +
        // button handler, and metadata updates go through it as well
        // (see UpdateSMTCMetadata in MainWindow.Playback.cpp).
        //
        // Disable MediaPlayer.CommandManager so the player doesn't ALSO
        // auto-register its own SMTC entry — without this, Win+V shows two
        // sessions for our app (one with proper title/artist, one with the
        // EXE path). CommandManager.IsEnabled defaults to true and is what
        // creates the auto-SMTC; turning it off leaves the window-bound
        // SMTC as the sole session.
        try
        {
            AudioPlayerService().GetMediaPlayer().CommandManager().IsEnabled(false);
        }
        catch (...) {}
        if (m_hwnd)
        {
            try
            {
                auto factory = winrt::get_activation_factory<
                    winrt::Windows::Media::SystemMediaTransportControls,
                    ISystemMediaTransportControlsInterop>();
                winrt::Windows::Media::SystemMediaTransportControls windowSmtc{ nullptr };
                winrt::check_hresult(factory->GetForWindow(
                    m_hwnd,
                    winrt::guid_of<winrt::Windows::Media::SystemMediaTransportControls>(),
                    winrt::put_abi(windowSmtc)));
                m_windowSmtc = windowSmtc;
            }
            catch (...) {}
        }

        auto smtc = m_windowSmtc
            ? m_windowSmtc
            : AudioPlayerService().GetMediaPlayer().SystemMediaTransportControls();
        smtc.IsPlayEnabled(true);
        smtc.IsPauseEnabled(true);
        smtc.IsNextEnabled(true);
        smtc.IsPreviousEnabled(true);
        smtc.IsEnabled(true);

        smtc.ButtonPressed([this, dispatcher](winrt::Windows::Media::SystemMediaTransportControls const& sender, winrt::Windows::Media::SystemMediaTransportControlsButtonPressedEventArgs const& args)
        {
            (void)sender;
            auto button = args.Button();
            dispatcher.TryEnqueue([this, button]()
            {
                switch (button)
                {
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Play:
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Pause:
                        PlayPauseButton_Click(nullptr, nullptr);
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Next:
                        NextButton_Click(nullptr, nullptr);
                        break;
                    case winrt::Windows::Media::SystemMediaTransportControlsButton::Previous:
                        PreviousButton_Click(nullptr, nullptr);
                        break;
                }
            });
        });

        // MediaPlayer.CommandManager owns the higher-level routing for the
        // SMTC events (Play / Pause / Next / Previous). Its defaults:
        //   - Play / Pause -> MediaPlayer.Play() / .Pause() directly
        //   - Next / Previous -> advance MediaPlaybackList; with no list
        //     (we shelved gapless), this is a silent no-op.
        // In either case the CommandManager consumes the event BEFORE the
        // ButtonPressed callback above can route it through our queue.
        // Hook the CommandManager events directly and mark args.Handled
        // so the defaults don't run, then call the same UI handlers the
        // on-screen transport buttons fire.
        auto cmdMgr = AudioPlayerService().GetMediaPlayer().CommandManager();
        cmdMgr.PlayReceived([this, dispatcher](
            winrt::Windows::Media::Playback::MediaPlaybackCommandManager const&,
            winrt::Windows::Media::Playback::MediaPlaybackCommandManagerPlayReceivedEventArgs const& args)
        {
            args.Handled(true);
            dispatcher.TryEnqueue([this]() { PlayPauseButton_Click(nullptr, nullptr); });
        });
        cmdMgr.PauseReceived([this, dispatcher](
            winrt::Windows::Media::Playback::MediaPlaybackCommandManager const&,
            winrt::Windows::Media::Playback::MediaPlaybackCommandManagerPauseReceivedEventArgs const& args)
        {
            args.Handled(true);
            dispatcher.TryEnqueue([this]() { PlayPauseButton_Click(nullptr, nullptr); });
        });
        cmdMgr.NextReceived([this, dispatcher](
            winrt::Windows::Media::Playback::MediaPlaybackCommandManager const&,
            winrt::Windows::Media::Playback::MediaPlaybackCommandManagerNextReceivedEventArgs const& args)
        {
            args.Handled(true);
            dispatcher.TryEnqueue([this]() { NextButton_Click(nullptr, nullptr); });
        });
        cmdMgr.PreviousReceived([this, dispatcher](
            winrt::Windows::Media::Playback::MediaPlaybackCommandManager const&,
            winrt::Windows::Media::Playback::MediaPlaybackCommandManagerPreviousReceivedEventArgs const& args)
        {
            args.Handled(true);
            dispatcher.TryEnqueue([this]() { PreviousButton_Click(nullptr, nullptr); });
        });

        // Re-apply the SMTC enable preference now that SMTC is configured.
        ApplyWindowsMediaControls();

        // Enumerate audio output devices and bring up Discord presence if on.
        PopulateOutputDevicesAsync();
        ApplyDiscordPresence();

        // Apply the persisted volume (0.0-1.0) to the transport + icon now so
        // the first playback is at the right level. The slider itself can only
        // be set once it is loaded/templated (setting Value in the ctor gets
        // re-coerced to the XAML default), so do that from its Loaded event.
        {
            double savedVolume = SettingsManagerService().GetVolume();
            // A persisted 0 means silence on launch (and historically came
            // from the mute-persist bug). Recover to an audible default.
            if (savedVolume <= 0.0001)
            {
                savedVolume = 0.7;
                SettingsManagerService().SetVolume(savedVolume);
            }
            AudioPlayerService().GetMediaPlayer().Volume(savedVolume);
            UpdateVolumeIcon(savedVolume);

            auto volRestoreToken = std::make_shared<winrt::event_token>();
            *volRestoreToken = VolumeSlider().Loaded(
                [this, volRestoreToken, savedVolume](winrt::Windows::Foundation::IInspectable const&,
                                                     winrt::Microsoft::UI::Xaml::RoutedEventArgs const&)
                {
                    // Slider is now in the visual tree, so this value sticks.
                    // m_suppressVolumePersist is still TRUE here (its default),
                    // so neither this set nor the earlier XAML-default
                    // ValueChanged(50) persisted and clobbered the saved level.
                    VolumeSlider().Value(savedVolume * 100.0);
                    // Apply transport + visuals directly in case ValueChanged
                    // did not fire (e.g. value already equal to the default).
                    TransportSetVolume(savedVolume);
                    AudioPlayerService().SetVolume(savedVolume);
                    UpdateVolumeIcon(savedVolume);
                    if (VolumeFillScale())
                    {
                        VolumeFillScale().ScaleX(savedVolume);
                    }
                    VolumeSlider().Loaded(*volRestoreToken); // run once
                    // Restore complete: from now on, genuine user changes persist.
                    m_suppressVolumePersist = false;
                });
        }

        // Set default navigation to Home
        NavigationService().NavigateTo(LastMusicPlayer::Frontend::NavPage::Home);
        UpdateNavSelection(L"Home");

        // Load previously saved music library
        auto savedProviderBaseUrl = ReadAppSettingString(L"ProviderBaseUrl");
        auto credentialMigration = LastMusicPlayer::Backend::MigrateLegacyProviderApiKey(
            SettingsManagerService(), CredentialStoreService());
        auto initialRemoteMode = RemoteMusicServiceService().Mode();
        auto savedProviderApiKey = initialRemoteMode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey
            ? CredentialStoreService().ReadProviderApiKey()
            : winrt::hstring{};
        if (initialRemoteMode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey)
        {
            // Stream-cache maintenance is part of API-key mode. LocalOnly and
            // Account startup do not inspect or mutate this provider-owned cache.
            StreamCacheService().PruneOnStartup();
        }
        if (!savedProviderBaseUrl.empty())
        {
            ProviderBaseUrlBox().Text(savedProviderBaseUrl);
        }
        ProviderApiKeyBox().Password(L"");
        auto migrationMessage = LastMusicPlayer::Backend::CredentialMigrationMessage(credentialMigration);
        if (!migrationMessage.empty())
        {
            ProviderTestStatusText().Text(migrationMessage);
        }
        else
        {
            ProviderTestStatusText().Text(
                !savedProviderBaseUrl.empty() && !savedProviderApiKey.empty()
                    ? L"Configured"
                    : L"Not configured");
        }
        if (!DatabaseService().IsInitialized())
        {
            DatabaseService().Initialize();
        }
        auto weakWindow = get_weak();
        AccountSessionService().SetOwnerChangedCallback([weakWindow](winrt::hstring const& ownerId)
        {
            RemoteMusicServiceService().InvalidateScope();
            if (DatabaseService().IsInitialized())
            {
                auto mode = RemoteMusicServiceService().Mode();
                if (mode == LastMusicPlayer::Backend::RemoteAccessMode::Account && ownerId.empty())
                {
                    RemoteMusicServiceService().SetMode(LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly);
                    mode = LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly;
                }
                auto accountMode = mode == LastMusicPlayer::Backend::RemoteAccessMode::Account && !ownerId.empty();
                auto modeName = accountMode
                    ? winrt::hstring{ L"Account" }
                    : LastMusicPlayer::Backend::RemoteAccessModeName(mode == LastMusicPlayer::Backend::RemoteAccessMode::Account
                        ? LastMusicPlayer::Backend::RemoteAccessMode::LocalOnly
                        : mode);
                DatabaseService().SetRemoteLibraryContext(
                    std::wstring(modeName.c_str()),
                    accountMode ? std::wstring(ownerId.c_str()) : std::wstring{});
            }
            if (auto window = weakWindow.get())
            {
                window->DispatcherQueue().TryEnqueue([weakWindow]()
                {
                    if (auto current = weakWindow.get())
                    {
                        current->InvalidateRemoteScopeWork();
                        current->RefreshAccountSettingsUi();
                    }
                });
            }
        });
        DatabaseService().SetRemoteLibraryContext(
            initialRemoteMode == LastMusicPlayer::Backend::RemoteAccessMode::ApiKey ? L"ApiKey" : L"LocalOnly");
        RefreshAccountSettingsUi();
        if (initialRemoteMode == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            RunDetached(RestoreAccountIntegrationAsync());
        }
        UpdateSongsScopeLabel();
        auto savedLibraryPath = ReadAppSettingString(L"MusicLibraryPath");
        if (!savedLibraryPath.empty())
        {
            // Show path in the Settings TextBox
            MusicFolderPathBox().Text(savedLibraryPath);
        }

        // Populate display-name surfaces before any async work starts so
        // the sidebar avatar/name + "Made for X" header don't briefly
        // render the static XAML defaults.
        ApplyUserDisplayName();

        QueueStartupDataLoad(savedLibraryPath);
    }

    void MainWindow::HomeButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        HomeViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        SettingsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SongsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        HomeCatalogViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ExitSearchMode();
        UpdateNavSelection(L"Home");
        m_catalogBackStack.clear();
        m_catalogSurface = CatalogSurface::Home;
        if (RemoteMusicServiceService().Mode() == LastMusicPlayer::Backend::RemoteAccessMode::Account)
        {
            RunDetached(HydrateDiscoverAsync(false));
        }
        else
        {
            HomeCatalogPrimaryContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            HomeCatalogMoodPanel().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }
    }

    void MainWindow::SettingsNav_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        HomeViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SettingsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        SongsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        HomeCatalogViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ExitSearchMode();
        UpdateNavSelection(L"Settings");
        UpdateSettingsSection(L"Profile");
        // Pre-populate Profile textbox with the persisted name (blank if
        // never set — placeholder text shows "Listener" instead).
        if (auto box = DisplayNameBox())
        {
            box.Text(SettingsManagerService().GetString(L"UserDisplayName", L""));
        }
        UpdateAboutStats();
    }

    void MainWindow::SongsButton_Click(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::RoutedEventArgs const& args)
    {
        (void)sender;
        (void)args;
        HomeViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SettingsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        SongsViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
        HomeCatalogViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        LibraryViewContainer().Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        ExitSearchMode();
        if (!m_songsResultsValid)
        {
            ApplySongsFilterSort();
        }
        UpdateNavSelection(L"Songs");
    }

    MainWindow::AppStateSnapshot MainWindow::BuildAppStateSnapshot()
    {
        AppStateSnapshot snapshot;

        auto currentTrack = AudioPlayerService().GetCurrentTrack();
        if (currentTrack)
        {
            if (!currentTrack.FilePath().empty() && !IsHttpUrl(currentTrack.FilePath()))
            {
                snapshot.LastTrackPath = currentTrack.FilePath();
            }
            else if (!currentTrack.RemoteId().empty())
            {
                snapshot.LastTrackPath = winrt::hstring(L"remote-id:" + std::wstring(currentTrack.RemoteId().c_str()));
            }
            else
            {
                snapshot.LastTrackPath = winrt::hstring(L"source:" + CatalogSourceKey(currentTrack));
            }
        }
        snapshot.Volume = AudioPlayerService().GetVolume();

        return snapshot;
    }

    winrt::hstring MainWindow::SerializeAppStateSnapshot(AppStateSnapshot const& snapshot)
    {
        winrt::Windows::Data::Json::JsonObject stateJson;
        stateJson.Insert(L"LastTrack", winrt::Windows::Data::Json::JsonValue::CreateStringValue(snapshot.LastTrackPath));
        stateJson.Insert(L"Volume", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(snapshot.Volume));

        return stateJson.Stringify();
    }

    MainWindow::AppStateSnapshot MainWindow::ParseAppStateSnapshot(winrt::hstring const& payload)
    {
        AppStateSnapshot snapshot;
        auto stateJson = winrt::Windows::Data::Json::JsonObject::Parse(payload);
        snapshot.LastTrackPath = stateJson.GetNamedString(L"LastTrack", L"");
        snapshot.Volume = stateJson.GetNamedNumber(L"Volume", snapshot.Volume);
        snapshot.HomePlaySequence = static_cast<uint64_t>((std::max)(0.0, stateJson.GetNamedNumber(L"HomePlaySequence", 0.0)));

        if (!stateJson.HasKey(L"HomeHistory") ||
            stateJson.Lookup(L"HomeHistory").ValueType() != winrt::Windows::Data::Json::JsonValueType::Array)
        {
            return snapshot;
        }

        auto history = stateJson.GetNamedArray(L"HomeHistory");
        snapshot.HomeHistory.reserve(history.Size());
        for (uint32_t i = 0; i < history.Size(); ++i)
        {
            auto value = history.GetAt(i);
            if (value.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object)
            {
                continue;
            }

            auto object = value.GetObject();
            AppStateTrackSnapshot track;
            track.Title = object.GetNamedString(L"title", L"");
            track.Artist = object.GetNamedString(L"artist", L"");
            track.Album = object.GetNamedString(L"album", L"");
            track.Genre = object.GetNamedString(L"genre", L"");
            track.FilePath = object.GetNamedString(L"filePath", L"");
            track.ArtworkUrl = object.GetNamedString(L"artworkUrl", L"");
            track.DateAdded = object.GetNamedString(L"dateAdded", L"");
            track.Duration = object.GetNamedString(L"duration", L"");
            track.SourceKind = object.GetNamedString(L"sourceKind", IsHttpUrl(track.FilePath) ? L"remote" : L"local");
            track.Provider = object.GetNamedString(L"provider", L"");
            track.SourceUrl = object.GetNamedString(L"sourceUrl", L"");
            track.SourceLabel = object.GetNamedString(L"sourceLabel", track.SourceKind == L"remote" ? L"Remote" : L"Local");
            track.Key = object.GetNamedString(L"key", L"");
            track.DurationSeconds = object.GetNamedNumber(L"durationSeconds", 0.0);
            track.DateAddedSortKey = object.GetNamedNumber(L"dateAddedSortKey", 0.0);
            track.IsLiked = object.GetNamedBoolean(L"isLiked", false);
            track.PlayCount = static_cast<uint32_t>((std::max)(0.0, object.GetNamedNumber(L"playCount", 0.0)));
            track.LastPlayedOrder = static_cast<uint64_t>((std::max)(0.0, object.GetNamedNumber(L"lastPlayedOrder", 0.0)));
            snapshot.HomeHistory.push_back(std::move(track));
        }

        return snapshot;
    }

    winrt::Last_Music_Player::TrackInfo MainWindow::TrackFromAppStateSnapshot(AppStateTrackSnapshot const& snapshot)
    {
        LastMusicPlayer::Backend::TrackInfo track;
        track.Title(snapshot.Title);
        track.Artist(snapshot.Artist);
        track.Album(snapshot.Album);
        track.Genre(snapshot.Genre);
        track.FilePath(snapshot.FilePath);
        track.ArtworkUrl(snapshot.ArtworkUrl);
        track.DateAdded(snapshot.DateAdded);
        track.Duration(snapshot.Duration);
        track.SourceKind(snapshot.SourceKind.empty() ? (IsHttpUrl(snapshot.FilePath) ? L"remote" : L"local") : snapshot.SourceKind);
        track.Provider(snapshot.Provider);
        track.SourceUrl(snapshot.SourceUrl);
        track.SourceLabel(snapshot.SourceLabel.empty() ? (track.SourceKind() == L"remote" ? L"Remote" : L"Local") : snapshot.SourceLabel);
        track.IsLiked(snapshot.IsLiked);
        track.DurationSeconds(snapshot.DurationSeconds);
        track.DateAddedSortKey(snapshot.DateAddedSortKey);

        ApplyMusicArtwork(track, track.ArtworkUrl(), L"track");
        return track;
    }

    void MainWindow::SaveAppState()
    {
        auto lease = UserDataOperationGateService().TryEnter();
        if (!lease)
        {
            return;
        }

        auto const version = g_appStateSaveVersion.fetch_add(1, std::memory_order_relaxed) + 1;

        winrt::hstring payload;
        try
        {
            payload = MainWindow::SerializeAppStateSnapshot(BuildAppStateSnapshot());
        }
        catch (...)
        {
            return;
        }

        WriteLatestAppStateAsync(std::move(payload), version, std::move(*lease));
    }

    bool MainWindow::ClearSavedAppState()
    {
        std::lock_guard lock{ g_appStateFileMutex };
        g_appStateSaveVersion.fetch_add(1, std::memory_order_release);

        auto path = StateFilePath();
        std::error_code stateError;
        std::filesystem::remove(path, stateError);

        std::error_code tempError;
        std::filesystem::remove(path.wstring() + L".tmp", tempError);
        return !stateError && !tempError;
    }

    void MainWindow::ApplyAppStateSnapshot(AppStateSnapshot const& snapshot)
    {
        try
        {
            // Volume is restored from Settings.json at startup (single
            // source of truth); the legacy app-state copy is ignored here
            // so it can't override the persisted setting on a 0-100 slider.

            auto legacyHistoryMigrated = SettingsManagerService().GetBool(L"LegacyHomeHistoryMigrated", false);
            if (!legacyHistoryMigrated)
            {
                m_homePlaySequence = (std::max)(m_homePlaySequence, snapshot.HomePlaySequence);

                std::unordered_set<std::wstring> seenKeys;
                for (auto const& existing : m_homeRecentHistory)
                {
                    auto key = HomeQueueDedupeKey(existing);
                    if (!key.empty())
                    {
                        seenKeys.insert(std::move(key));
                    }
                }

                for (auto const& savedTrack : snapshot.HomeHistory)
                {
                    auto track = MainWindow::TrackFromAppStateSnapshot(savedTrack);
                    std::wstring key = savedTrack.Key.empty() ? HomeQueueDedupeKey(track) : std::wstring(savedTrack.Key.c_str());
                    if (key.empty() || seenKeys.find(key) != seenKeys.end() || LocalFileMissing(track))
                    {
                        continue;
                    }

                    m_homePlayCounts[key] = (std::max)(m_homePlayCounts[key], savedTrack.PlayCount);
                    m_homeLastPlayedOrder[key] = (std::max)(m_homeLastPlayedOrder[key], savedTrack.LastPlayedOrder);
                    m_homePlaySequence = (std::max)(m_homePlaySequence, savedTrack.LastPlayedOrder);
                    m_homeRecentHistory.push_back(track);
                    seenKeys.insert(key);

                    if (DatabaseService().IsInitialized())
                    {
                        auto catalogKey = CatalogSourceKey(track);
                        if (!catalogKey.empty())
                        {
                            auto remote = ToLowerCopy(track.SourceKind()) == L"remote" || (!track.File() && IsHttpUrl(track.FilePath()));
                            if (remote)
                            {
                                DatabaseService().UpsertRemoteTrack(track, catalogKey);
                            }
                            else if (!track.FilePath().empty())
                            {
                                DatabaseService().UpsertLocalTrack(track, catalogKey);
                            }
                            DatabaseService().MergePlaybackStats(catalogKey, savedTrack.PlayCount, savedTrack.LastPlayedOrder);
                        }
                    }
                }

                if (DatabaseService().IsInitialized())
                {
                    SettingsManagerService().SetBool(L"LegacyHomeHistoryMigrated", true);
                }
            }

            if (DatabaseService().IsInitialized())
            {
                m_homePlaySequence = (std::max)(m_homePlaySequence, DatabaseService().MaxLastPlayedOrder());
            }

            // Resume-on-launch: surface the last-played track in the player bar,
            // ready to resume. Skips it if the file was deleted off disk.
            RestoreLastPlayedTrack(snapshot);
        }
        catch (...)
        {
        }
    }

    void MainWindow::LoadAppState()
    {
        try
        {
            auto jsonPayload = ReadTextFile(StateFilePath());
            if (!jsonPayload.empty())
            {
                ApplyAppStateSnapshot(MainWindow::ParseAppStateSnapshot(jsonPayload));
            }
        }
        catch (...)
        {
        }
    }

    void MainWindow::EnsureAccentBrushes()
    {
        if (m_accentBrushesCaptured)
        {
            return;
        }
        // Capture resolved brushes off the hidden BrushProbes panel. These
        // elements are bound to the theme resources and are NEVER reassigned,
        // so they stay theme-correct on re-capture (unlike the nav elements,
        // whose Foreground/Background we overwrite in UpdateNavSelection).
        m_brushAccent = ProbeAccent().Foreground();
        m_brushAccentSoft = ProbeAccentSoft().Background();
        m_brushGlyphIdle = ProbeGlyphIdle().Foreground();
        m_brushLabelIdle = ProbeLabelIdle().Foreground();
        m_brushTransparent = ProbeTransparent().Background();
        m_brushStroke = ProbeStroke().BorderBrush();
        m_accentBrushesCaptured = true;
    }

    void MainWindow::UpdateNavSelection(winrt::hstring const& key)
    {
        m_currentNav = key;
        EnsureAccentBrushes();

        using winrt::Microsoft::UI::Xaml::Visibility;
        using winrt::Microsoft::UI::Text::FontWeights;

        struct NavRow
        {
            winrt::Microsoft::UI::Xaml::Controls::Button btn;
            winrt::Microsoft::UI::Xaml::Controls::FontIcon glyph;
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label;
            winrt::hstring id;
        };

        NavRow rows[] = {
            { HomeButton(), HomeGlyph(), HomeLabel(), L"Home" },
            { SongsButton(), SongsGlyph(), SongsLabel(), L"Songs" },
            { LibraryButton(), LibraryGlyph(), LibraryLabel(), L"Library" },
            { SettingsButton(), SettingsGlyph(), SettingsLabel(), L"Settings" },
        };

        for (auto const& r : rows)
        {
            bool selected = (key == r.id);
            r.btn.Background(selected ? m_brushAccentSoft : m_brushTransparent);
            r.glyph.Foreground(selected ? m_brushAccent : m_brushGlyphIdle);
            r.label.Foreground(selected ? m_brushAccent : m_brushLabelIdle);
            r.label.FontWeight(selected ? FontWeights::SemiBold() : FontWeights::Normal());
        }
    }

    void MainWindow::Card_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        // -2px Y translate is a subtle hover "lift" — keep it; this is the
        // desktop affordance that compensates for the removed play overlay.
        if (auto root = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>())
        {
            root.Translation(winrt::Windows::Foundation::Numerics::float3{ 0.0f, -2.0f, 0.0f });
        }
    }

    void MainWindow::Card_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        if (auto root = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>())
        {
            root.Translation(winrt::Windows::Foundation::Numerics::float3{ 0.0f, 0.0f, 0.0f });
        }
    }

    void MainWindow::PlaylistCard_PointerEntered(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        // Synchronous handler: an escaping exception calls std::terminate/abort().
        try
        {
            if (auto root = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                root.Translation(winrt::Windows::Foundation::Numerics::float3{ 0.0f, -2.0f, 0.0f });
            }
        }
        catch (...) {}
    }

    void MainWindow::PlaylistCard_PointerExited(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& e)
    {
        (void)e;
        try
        {
            if (auto root = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>())
            {
                root.Translation(winrt::Windows::Foundation::Numerics::float3{ 0.0f, 0.0f, 0.0f });
            }
        }
        catch (...) {}
    }

    void MainWindow::RailTab_UpNext_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e)
    {
        (void)sender;
        (void)e;
        SetRailTab(true);
    }

    void MainWindow::RailTab_Lyrics_Tapped(winrt::Windows::Foundation::IInspectable const& sender, winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const& e)
    {
        (void)sender;
        (void)e;
        SetRailTab(false);
    }

    void MainWindow::SetRailTab(bool upNext)
    {
        EnsureAccentBrushes();
        winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(RailTabThumb(), upNext ? 0 : 1);
        // m_brushLabelIdle == primary text (active tab), m_brushGlyphIdle == secondary (inactive).
        UpNextTabText().Foreground(upNext ? m_brushLabelIdle : m_brushGlyphIdle);
        LyricsTabText().Foreground(upNext ? m_brushGlyphIdle : m_brushLabelIdle);

        if (auto thumb = FsRailTabThumb())
        {
            winrt::Microsoft::UI::Xaml::Controls::Grid::SetColumn(thumb, upNext ? 0 : 1);
        }
        if (auto text = FsUpNextTabText())
        {
            text.Foreground(upNext ? m_brushLabelIdle : m_brushGlyphIdle);
        }
        if (auto text = FsLyricsTabText())
        {
            text.Foreground(upNext ? m_brushGlyphIdle : m_brushLabelIdle);
        }

        auto upVis = upNext ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
        auto lyVis = upNext ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed : winrt::Microsoft::UI::Xaml::Visibility::Visible;
        if (auto panel = UpNextContentPanel()) panel.Visibility(upVis);
        if (auto panel = LyricsContentPanel()) panel.Visibility(lyVis);
        if (auto panel = FsUpNextContentPanel()) panel.Visibility(upVis);
        if (auto panel = FsLyricsContentPanel()) panel.Visibility(lyVis);

        m_railOnLyrics = !upNext;
        ApplyRightRailWidth();
        if (m_railOnLyrics)
        {
            HydrateLyricsForCurrentTrack();
        }
    }

    void MainWindow::ApplyRightRailWidth()
    {
        auto column = RightRailColumn();
        auto body = MainBodyGrid();
        if (!column || !body) return;

        // < 1100px the rail is collapsed (VSM sets Width=0). Don't fight it.
        auto width = body.ActualWidth();
        if (width < 1100.0) return;

        // Don't override the QueueButton-forced collapse (width 0). The forced-show
        // path sets 320 explicitly which we *do* want to upgrade when on Lyrics.
        auto currentWidth = column.Width();
        if (currentWidth.GridUnitType == winrt::Microsoft::UI::Xaml::GridUnitType::Pixel
            && currentWidth.Value <= 1.0)
        {
            return;
        }

        double target;
        if (m_railOnLyrics)
        {
            target = (width >= 1500.0) ? 460.0 : 380.0;
        }
        else
        {
            target = (width >= 1500.0) ? 380.0 : 300.0;
        }
        column.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(target));
    }

    void MainWindow::OnAppWindowClosing(winrt::Microsoft::UI::Windowing::AppWindow const& sender, winrt::Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args)
    {
        (void)sender;
        FlushPendingVolumePersist();

        // Persist window geometry so next launch restores the user's
        // chosen size + position instead of reverting to the hardcoded
        // default. Read via GetWindowRect (covers both user-driven
        // and programmatic moves). Skip if the window is currently
        // minimized — that would persist nonsense coordinates.
        if (m_hwnd && !::IsIconic(m_hwnd))
        {
            RECT r{};
            if (::GetWindowRect(m_hwnd, &r))
            {
                auto& settings = SettingsManagerService();
                int w = r.right - r.left;
                int h = r.bottom - r.top;
                if (w >= 800 && h >= 600)
                {
                    settings.SetInt(L"WindowWidth", w);
                    settings.SetInt(L"WindowHeight", h);
                    settings.SetInt(L"WindowX", r.left);
                    settings.SetInt(L"WindowY", r.top);
                }
            }
        }
        if (m_forceExit)
        {
            m_cast.Disconnect();
            ClearCastCallbacks();
            return; // allow the close to proceed
        }
        // CloseBehaviorCombo: index 0 = minimize to tray, 1 = quit.
        if (SettingsManagerService().GetInt(L"CloseBehavior", 0) == 0)
        {
            args.Cancel(true);
            EnsureTrayIcon();
            if (m_appWindow)
            {
                m_appWindow.Hide();
            }
        }
        else
        {
            m_cast.Disconnect();
            ClearCastCallbacks();
        }
    }

    void MainWindow::AccelPrev_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        (void)sender;
        PreviousButton_Click(nullptr, nullptr);
        args.Handled(true);
    }

    void MainWindow::AccelNext_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        (void)sender;
        NextButton_Click(nullptr, nullptr);
        args.Handled(true);
    }

    void MainWindow::AccelPlayPause_Invoked(winrt::Microsoft::UI::Xaml::Input::KeyboardAccelerator const& sender, winrt::Microsoft::UI::Xaml::Input::KeyboardAcceleratorInvokedEventArgs const& args)
    {
        (void)sender;
        // Don't hijack Space while the user is typing in a text field.
        auto focused = winrt::Microsoft::UI::Xaml::Input::FocusManager::GetFocusedElement(this->Content().XamlRoot());
        if (focused && focused.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>())
        {
            args.Handled(false);
            return;
        }
        PlayPauseButton_Click(nullptr, nullptr);
        args.Handled(true);
    }

}
