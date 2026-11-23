#pragma once

#include <QTimer>

#include <QPointer>

#include <memory>

#include "core/account/AccountClient.h"
#include "core/account/AccountDataLifecycleService.h"
#include "core/account/AccountLibraryEditor.h"
#include "core/account/AccountSessionService.h"
#include "core/account/AccountSignOutCoordinator.h"
#include "core/account/AccountSyncScheduler.h"
#include "core/account/MusicSyncService.h"
#include "core/account/ScopedCredentialStore.h"
#include "core/account/SecretStore.h"
#include "core/app/AppPaths.h"
#include "core/catalog/CatalogClient.h"
#include "core/home/HomeModel.h"
#include "core/catalog/CatalogService.h"
#include "core/config/PrivateConfig.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRepository.h"
#include "core/db/CollectionRepository.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/Database.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/ArtworkCache.h"
#include "app/CloseToTrayController.h"
#include "app/WindowPlacementKeeper.h"
#include "core/app/UserDataCleanup.h"
#include "core/library/ArtistFollowService.h"
#include "core/library/CollectionActions.h"
#include "core/library/HomeMixService.h"
#include "core/downloads/DownloadManager.h"
#include "core/downloads/HttpDownloadFetcher.h"
#include "core/library/LibraryScanner.h"
#include "core/library/LinkImportService.h"
#include "core/lyrics/LyricsService.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/ModeService.h"
#include "core/net/HttpClient.h"
#include "core/playback/AccountTrackSourceResolver.h"
#include "core/playback/AutoplayService.h"
#include "core/playback/MediaControlsBridge.h"
#include "core/playback/PlaybackClock.h"
#include "core/playback/HttpStreamDownloader.h"
#include "core/playback/PlaybackStatePersistence.h"
#include "core/playback/ProviderTrackSourceResolver.h"
#include "core/playback/StreamCache.h"
#include "core/playback/PlayerAutoplayQueue.h"
#include "core/playback/PlaybackSourceRouter.h"
#include "core/playback/PlayerController.h"
#include "core/playback/TrackSourceResolver.h"
#include "core/provider/ProviderClient.h"
#include "core/remote/ArtworkService.h"
#include "core/catalog/CatalogDetailParser.h"
#include "core/remote/RemoteMusicService.h"
#include "core/settings/SettingsStore.h"
#include "platform/ISingleInstance.h"
#include "platform/windows/WindowsAudioPlayer.h"
#include "platform/windows/WindowsCredentialStore.h"
#include "platform/windows/WindowsExternalLinks.h"
#include "platform/windows/WindowsFolderPicker.h"
#include "platform/windows/WindowsMediaControls.h"
#include "platform/windows/WindowsNetworkStatus.h"
#include "platform/windows/WindowsAudioOutputs.h"
#include "platform/windows/WindowsPowerStatus.h"
#include "platform/windows/WindowsTray.h"
#include "platform/windows/WindowsReducedMotion.h"
#include "platform/windows/WindowsTagReader.h"
#include "platform/windows/WindowsWindowChrome.h"
#include "ui/navigation/Navigator.h"
#include "ui/player/AmbientAccentSampler.h"
#include "ui/player/FullscreenPlayerViewModel.h"
#include "ui/player/PlaybackNoticeToasts.h"
#include "ui/player/PlayerBarViewModel.h"
#include "ui/player/QueueRailViewModel.h"
#include "ui/settings/FirstRunViewModel.h"
#include "ui/home/HomeViewModel.h"
#include "platform/windows/WindowsWindowPlacement.h"
#include "ui/search/SearchSource.h"
#include "ui/search/SearchViewModel.h"
#include "ui/library/AccountArtistsViewModel.h"
#include "ui/catalog/CatalogArtistViewModel.h"
#include "ui/downloads/OfflineViewModel.h"
#include "ui/settings/AudioOutputViewModel.h"
#include "ui/player/LyricsViewModel.h"
#include "ui/share/ShareController.h"
#include "ui/catalog/CatalogPageViewModel.h"
#include "ui/settings/RegionLanguagesViewModel.h"
#include "ui/shell/ConnectivityViewModel.h"
#include "ui/welcome/WelcomeViewModel.h"
#include "ui/library/CollectionEditor.h"
#include "ui/library/CollectionViewModel.h"
#include "ui/library/LibraryViewModel.h"
#include "ui/library/LocalArtistViewModel.h"
#include "ui/settings/DataSettingsViewModel.h"
#include "ui/settings/LibrarySettingsViewModel.h"
#include "ui/tracks/TrackMenuController.h"
#include "ui/settings/SettingsAboutViewModel.h"
#include "ui/settings/SettingsViewModel.h"
#include "ui/settings/SourceModeViewModel.h"
#include "ui/shell/AppRoot.h"
#include "ui/shell/ProfileViewModel.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/shell/SidebarViewModel.h"
#include "ui/shell/WindowChromeViewModel.h"
#include "ui/theme/Theme.h"
#include "ui/theme/ThemeSettingsBinding.h"
#include "ui/toasts/ToastQueue.h"

class QQmlApplicationEngine;
class QQmlEngine;

namespace lmp::app {

// Builds every platform object, service and shell view model once, in
// dependency order (the member order below), and destroys them in reverse
// order. Nothing else in the application constructs services. Create it
// after the application object and before the QML engine; destroy the
// engine first.
//
// Threads: every service here lives on the GUI thread. The database has one
// owner thread, and for now that is the GUI thread: the player controller
// and the playback state write a few small rows (history, resume points)
// through the repositories there. Services do their file work on their own
// workers.
class CompositionRoot {
public:
    CompositionRoot();
    // Shuts the playback services down (saving the playback state) before
    // anything is destroyed.
    ~CompositionRoot();

    CompositionRoot(const CompositionRoot&) = delete;
    CompositionRoot& operator=(const CompositionRoot&) = delete;

    // True when this is the only running copy. Otherwise brings the running
    // copy's window to the front and returns false; the caller then exits.
    [[nodiscard]] bool claimSingleInstance();

    // Connects the engine's singletons to the services behind them. Call
    // once, after the engine exists and before QML is loaded.
    void attachEngine(QQmlEngine& engine);

    // Takes over the frame of the main window the engine loaded, then shows
    // it. Returns false when there is no window to show.
    [[nodiscard]] bool showMainWindow(QQmlApplicationEngine& engine);

    // Opens the welcome flow for review (the Debug build's --show-welcome).
    void showWelcomeForReview();
    // Opens Browse and searches for `query` (the Debug build's --search).
    void showSearchForReview(const QString& query);

private:
    // The account library as the home model reads it, in Account mode only.
    [[nodiscard]] std::optional<core::home::HomeLibrary> accountHomeLibrary() const;
    // A call in the signed-in account's name; its body, or the failure. A
    // rejected session signs out (when it is still the current one).
    [[nodiscard]] QFuture<core::Result<QByteArray>> sendAccount(core::net::HttpMethod method, const QString& pathAndQuery,
                                                                const QByteArray& jsonBody = {});

public:

private:
    // Every view that shows the library reads it again.
    void refreshLibraryViews();

    std::unique_ptr<platform::ISingleInstance> m_singleInstance;
    std::unique_ptr<platform::windows::WindowsReducedMotion> m_reducedMotion;
    platform::windows::WindowsWindowChrome m_windowChrome;

    core::AppPaths m_paths;
    core::SettingsStore m_settings;
    core::db::Database m_database;
    // Set right after m_database is built: whether it opened. Without it the
    // app runs on and plays, but nothing reaches the history.
    bool m_databaseOpen = false;
    core::db::TrackRepository m_tracks;
    // The cached account library (songs, playlists, sync bookkeeping).
    core::db::AccountRepository m_accounts;
    core::db::LibraryQueries m_library;
    core::db::CollectionRepository m_collections;
    core::db::GroupQueries m_groups;
    core::UserDataOperationGate m_userDataGate;
    // The music folder scan: tags and system thumbnails, the cover cache, the scanner.
    platform::windows::WindowsTagReader m_tagReader;
    core::ArtworkCache m_artworkCache;
    core::LibraryScanner m_scanner;
    // The music source: the account session (restored at launch), the API
    // key provider and the mode that chooses between them.
    platform::windows::WindowsExternalLinks m_externalLinks;
    platform::windows::WindowsCredentialStore m_credentials;
    // The credentials as the app uses them: under their own names in a
    // developer run on another data folder, so it never touches the real ones.
    core::account::ScopedCredentialStore m_scopedCredentials;
    core::account::SecretStore m_secrets;
    core::net::HttpClient m_http;
    core::PrivateConfig m_privateConfig;
    core::account::AccountClient m_accountClient;
    core::account::AccountSessionService m_session;
    core::provider::ProviderClient m_providerClient;
    core::mode::ModeService m_mode;
    // The account service's catalogue calls, and the one way to the remote
    // music source (account or provider, by mode).
    core::catalog::CatalogClient m_catalogClient;
    core::remote::RemoteMusicService m_remote;
    platform::windows::WindowsAudioPlayer m_audio;
    // Bound to the main window once it exists (showMainWindow).
    platform::windows::WindowsMediaControls m_mediaControls;
    // Where each song's audio comes from: files on this PC, or the account
    // service for remote songs in Account mode.
    platform::windows::WindowsNetworkStatus m_network;
    core::LocalFileSourceResolver m_localResolver;
    core::AccountTrackSourceResolver m_accountResolver;
    // API key mode: songs fetched ahead to disk, and the provider resolver.
    core::HttpStreamDownloader m_streamDownloader;
    core::StreamCache m_streamCache;
    core::ProviderTrackSourceResolver m_providerResolver;
    core::PlaybackSourceRouter m_resolver;
    // Offline downloads: their state file, transfers and the rules.
    platform::windows::WindowsPowerStatus m_power;
    core::HttpDownloadFetcher m_downloadFetcher;
    core::DownloadManager m_downloads;
    QTimer m_downloadRules;
    core::SystemPlaybackClock m_clock;
    core::PlayerController m_player;
    core::PlaybackStatePersistence m_persistence;
    core::ProviderStreamPrefetcher m_prefetcher;
    core::MediaControlsBridge m_mediaBridge;
    // Cover pictures from the network (the account relay in Account mode).
    core::remote::ArtworkService m_artwork;
    // The account library on this PC: changes queued here, the sync that
    // sends them and brings the library down, and when it runs.
    core::db::AccountOutboxRepository m_outbox;
    core::account::MusicSyncService m_sync;
    core::account::AccountSyncScheduler m_syncScheduler;
    // Signing out: a last sync, the service's logout, then the account's
    // data here (finished at the next launch if it could not be then).
    core::account::AccountDataLifecycleService m_signOut;
    // What the user changes in the account library from this PC.
    core::account::AccountLibraryEditor m_accountEditor;
    // Followed artists: on the account in Account mode, on this PC otherwise.
    core::db::FollowedArtistRepository m_followedArtists;
    core::ArtistFollowService m_artistFollows;
    // The account service's catalogue: the cache over its calls.
    core::catalog::CatalogService m_catalog;
    platform::windows::WindowsFolderPicker m_folderPicker;
    // What a song's menu and buttons do.
    core::TrackActions m_trackActions;
    // Making, renaming and deleting the user's playlists and albums.
    core::CollectionActions m_collectionActions;
    // Today's generated mixes (Home, Library, mix pages).
    core::HomeMixService m_homeMixes;

    ui::WindowChromeViewModel m_windowChromeViewModel;
    ui::Navigator m_navigator;
    ui::ShellLayoutViewModel m_layout;
    ui::SidebarViewModel m_sidebar;
    ui::ToastQueue m_toasts;
    ui::PlayerBarViewModel m_playerBar;
    // Tells the user when a remote song cannot play.
    ui::PlaybackNoticeToasts m_playbackNotices;
    ui::FullscreenPlayerViewModel m_fullscreen;
    ui::QueueRailViewModel m_queueRail;
    ui::AmbientAccentSampler m_ambient;
    ui::ThemeSettingsBinding m_themeBinding;
    ui::SettingsViewModel m_settingsViewModel;
    ui::SettingsAboutViewModel m_settingsAbout;
    ui::SourceModeViewModel m_source;
    ui::FirstRunViewModel m_firstRun;
    ui::ProfileViewModel m_profile;
    ui::LibrarySettingsViewModel m_librarySettings;
    ui::TrackMenuController m_trackMenu;
    ui::LibraryViewModel m_libraryPage;
    ui::CollectionViewModel m_collectionPage;
    ui::LocalArtistViewModel m_artistPage;
    // The Artists tab in Account mode: the account service's artist list.
    ui::AccountArtistsViewModel m_accountArtists;
    // The welcome flow a new account sees once.
    ui::WelcomeViewModel m_welcome;
    // The offline banner.
    ui::ConnectivityViewModel m_connectivity;
    // Settings: the music region and recommendation languages.
    ui::RegionLanguagesViewModel m_regionLanguages;
    // The catalogue's album, playlist, chart and shelf pages, and its artists.
    ui::CatalogPageViewModel m_catalogPage;
    ui::CatalogArtistViewModel m_catalogArtist;
    // Lyrics of the playing song, asked for in the account's scope.
    core::lyrics::LyricsService m_lyricsService;
    // Autoplay's top-up and "Start mix", over the player's queue.
    core::PlayerAutoplayQueue m_autoplayQueue;
    core::AutoplayService m_autoplay;
    // Albums and playlists imported from a provider link.
    core::LinkImportService m_linkImport;
    ui::LyricsViewModel m_lyrics;
    // The Offline page and the download controls.
    ui::OfflineViewModel m_offline;
    // The output device choice.
    platform::windows::WindowsAudioOutputs m_audioOutputs;
    ui::AudioOutputViewModel m_audioOutput;
    // Share links for songs, lists and artists (Account mode).
    ui::ShareController m_share;
    ui::CollectionEditor m_collectionEditor;
    ui::HomeViewModel m_home;
    // Browse and search (the local library; remote sources join in P08 and P09).
    ui::LocalSearchSource m_localSearch;
    ui::SearchViewModel m_search;
    // "Delete all data" and the Data card.
    core::UserDataCleanup m_cleanup;
    ui::DataSettingsViewModel m_dataSettings;
    // The main window's size and place between runs.
    platform::windows::WindowsWindowPlacement m_windowPlacement;
    WindowPlacementKeeper m_placementKeeper;
    // Closing the window to the tray, and the tray icon's menu.
    platform::windows::WindowsTray m_tray;
    CloseToTrayController m_closeToTray;
    ui::AppRoot m_appRoot;
    QPointer<ui::Theme> m_theme; // owned by the QML engine
};

} // namespace lmp::app
