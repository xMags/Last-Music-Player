#include "app/CompositionRoot.h"
#include "core/cast/CastTransport.h"
#include "core/connect/ConnectProtocol.h"
#include "core/connect/WebSocketConnectSocket.h"
#include "core/model/DisplayTitle.h"

#include "core/home/HomeModel.h"
#include "core/presence/PresenceCard.h"
#include "core/presence/PresenceProtocol.h"
#include "ui/images/ChartArtImageProvider.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDate>
#include <QDateTime>

#include <chrono>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QRandomGenerator>
#include <QString>
#include <QWindow>

#include <memory>
#include <optional>

#include "core/app/AppIdentity.h"
#include "core/async/Result.h"
#include "core/mode/SourceChoice.h"
#include "core/settings/SettingKeys.h"
#include "core/playback/QueueEngine.h"
#include "platform/windows/WindowsAppIdentity.h"
#include "platform/windows/WindowsSingleInstance.h"
#include "ui/images/ArtworkImageProvider.h"
#include "ui/player/CoverSource.h"
#include "ui/player/EmbeddedCoverImageProvider.h"
#include "ui/theme/FontLoader.h"
#include "ui/theme/Motion.h"

namespace lmp::app {

namespace {

Q_LOGGING_CATEGORY(lcComposition, "lmp.app.composition")

// Opens the library database on this (the GUI) thread, its owner. A failure
// is logged and the app runs on without it: songs still play, but plays and
// resume points are not stored, and the history stays empty.
bool openDatabase(core::db::Database& database)
{
    const core::Result<void> opened = database.initialize();
    if (!opened) {
        qCWarning(lcComposition) << "The library database did not open; running without history:"
                                 << opened.failure().detail;
        return false;
    }
    return true;
}

// Whether an account session is stored. A credential that cannot be read
// counts as stored, so an upgrading user is never shown the first-run page
// by a read error.
bool storedSessionExists(core::account::SecretStore& secrets)
{
    const core::Result<std::optional<QString>> session = secrets.readAccountSession();
    return !session || session.value().has_value();
}

// This PC's id on the device link: the saved one, or a new one saved now.
QString connectDeviceId(core::SettingsStore& settings)
{
    const QString saved = settings.string(core::settings::keys::ConnectDeviceId);
    if (core::connect::isValidDeviceId(saved)) {
        return saved;
    }
    const QString created = core::connect::newDeviceId();
    settings.setString(core::settings::keys::ConnectDeviceId, created);
    return created;
}

} // namespace

CompositionRoot::CompositionRoot()
    : m_singleInstance(std::make_unique<platform::windows::WindowsSingleInstance>(
          core::AppIdentity::singleInstanceMutexName(), core::AppIdentity::displayName()))
    , m_paths(core::AppPaths::forCurrentUser())
    , m_settings(m_paths.settingsFile())
    , m_database(m_paths)
    , m_databaseOpen(openDatabase(m_database))
    , m_tracks(m_database)
    , m_accounts(m_database)
    , m_library(m_database, &m_accounts)
    , m_collections(m_database)
    , m_groups(m_database)
    , m_artworkCache(m_paths.thumbsFolder(), &m_tagReader)
    , m_scanner({.tags = m_tagReader,
                 .artwork = m_artworkCache,
                 .tracks = m_tracks,
                 .library = m_library,
                 .settings = m_settings,
                 .gate = m_userDataGate})
    , m_externalLinks(m_paths.root())
    , m_scopedCredentials(m_credentials,
                          core::account::ScopedCredentialStore::suffixForDataRoot(
                              m_paths.root(), core::AppPaths::developerDataRoot()))
    , m_secrets(m_scopedCredentials, m_settings)
    , m_privateConfig(core::PrivateConfig::fromBuild())
    , m_accountClient(m_http, m_privateConfig)
    , m_session({.client = m_accountClient,
                 .secrets = m_secrets,
                 .settings = m_settings,
                 .links = m_externalLinks,
                 .operationGate = m_userDataGate})
    , m_providerClient(m_http)
    , m_mode({.settings = m_settings,
              .secrets = m_secrets,
              .session = m_session,
              .provider = m_providerClient,
              .accounts = m_accounts,
              .operationGate = m_userDataGate})
    , m_catalogClient(m_accountClient)
    , m_remote({.mode = m_mode,
                .session = m_session,
                .account = m_accountClient,
                .catalog = m_catalogClient,
                .provider = m_providerClient})
    , m_accountResolver({.remote = m_remote,
                         .settings = m_settings,
                         .network = m_network,
                         .mediaOrigin = m_privateConfig.accountMediaOrigin,
                         .now = {}})
    , m_streamCache(m_paths.streamCacheFolder(), m_userDataGate, m_streamDownloader)
    , m_providerResolver({.remote = m_remote, .mode = m_mode, .cache = m_streamCache, .tracks = m_tracks})
    , m_resolver(m_localResolver, m_accountResolver, m_providerResolver, m_mode)
    , m_downloads({.settings = m_settings,
                   .gate = m_userDataGate,
                   .fetcher = m_downloadFetcher,
                   .stateFile = m_paths.downloadsFile(),
                   .defaultFolder = m_paths.offlineFolder(),
                   .resolve =
                       [this](const core::DownloadItem& item) {
                           core::Track track;
                           track.sourceKind = QStringLiteral("remote");
                           track.sourceUrl = item.sourceUrl;
                           track.sourceTag = item.provider;
                           track.title = item.title;
                           track.artist = item.artist;
                           return m_resolver.refresh(track);
                       },
                   .scopePrefix =
                       [this]() {
                           return QStringLiteral("download|")
                               + core::track_keys::remoteScopeCacheKey(m_remote.scopeKeyParts()) + QLatin1Char('\n');
                       },
                   .metered = [this]() { return m_network.metered() || !m_network.online(); },
                   .onBattery = [this]() { return m_power.onBattery(); }})
    , m_player({.audio = m_audio, .settings = m_settings, .tracks = m_tracks, .resolver = m_resolver, .clock = m_clock},
               core::QueueEngine())
    , m_persistence({.player = m_player,
                     .settings = m_settings,
                     .tracks = m_tracks,
                     .library = m_library,
                     .userDataGate = m_userDataGate,
                     .clock = m_clock},
                    m_paths)
    , m_prefetcher({.player = m_player, .remote = m_remote, .mode = m_mode, .cache = m_streamCache})
    , m_mediaBridge({.player = m_player, .controls = m_mediaControls, .settings = m_settings, .clock = m_clock})
    , m_artwork({.client = m_accountClient, .session = m_session, .mode = m_mode, .http = m_http, .clock = m_clock})
    , m_outbox(m_database)
    , m_sync({.transport = m_accountClient,
              .session = m_session,
              .mode = m_mode,
              .accounts = m_accounts,
              .outbox = m_outbox,
              .settings = m_settings,
              .gate = m_userDataGate,
              .clock = m_clock,
              .nowUtc = {}})
    , m_syncScheduler({.sync = m_sync, .session = m_session, .mode = m_mode, .network = m_network, .clock = m_clock})
    , m_signOut({.session = m_session,
                 .outboundSync = m_sync,
                 .storage = m_accounts,
                 .settings = m_settings,
                 .gate = m_userDataGate,
                 .idleTimeout = std::chrono::seconds(30)})
    , m_accountEditor({.client = m_accountClient,
                       .session = m_session,
                       .mode = m_mode,
                       .accounts = m_accounts,
                       .outbox = m_outbox,
                       .sync = m_sync,
                       .gate = m_userDataGate,
                       .nowUtc = {},
                       .newEventId = {}})
    , m_followedArtists(m_database)
    , m_artistFollows({.client = m_accountClient,
                       .session = m_session,
                       .mode = m_mode,
                       .followed = m_followedArtists,
                       .gate = m_userDataGate})
    , m_catalog({.client = m_catalogClient,
                 .session = m_session,
                 .accounts = m_accounts,
                 .settings = m_settings,
                 .clock = m_clock,
                 .tags = core::catalog::SourceTags::fromConfig(m_privateConfig)})
    , m_trackActions({.player = m_player,
                      .tracks = m_tracks,
                      .collections = m_collections,
                      .groups = m_groups,
                      .mode = m_mode,
                      .gate = m_userDataGate,
                      .account = &m_accountEditor})
    , m_collectionActions(m_collections, m_userDataGate)
    , m_homeMixes(m_library, m_groups, [] { return QDate::currentDate(); })
    , m_connectClient({.requestTicket =
                           [this]() {
                               return sendAccount(core::net::HttpMethod::Post,
                                                  QStringLiteral("/v1/music/connect/ticket"), QByteArrayLiteral("{}"));
                           },
                       .apiOrigin = [this]() { return m_accountClient.baseOrigin(); },
                       .makeSocket = []() -> std::unique_ptr<core::connect::IConnectSocket> {
                           return std::make_unique<core::connect::WebSocketConnectSocket>();
                       },
                       .clock = m_clock,
                       .random = []() { return QRandomGenerator::global()->generateDouble(); },
                       .deviceId = connectDeviceId(m_settings),
                       .deviceName = QStringLiteral("Windows PC")})
    , m_connectSync({.client = m_connectClient,
                     .player = m_player,
                     .clock = m_clock,
                     .castDeviceName =
                         [this]() {
                             return m_cast.phase() == core::cast::CastController::Phase::Casting ? m_cast.deviceName()
                                                                                                 : QString();
                         }})
    , m_windowChromeViewModel(&m_windowChrome)
    , m_sidebar({.navigator = m_navigator,
                 .collections = m_collectionActions,
                 .actions = m_trackActions})
    , m_playerBar(m_player, &m_connectSync)
    , m_playbackNotices(m_player, m_toasts)
    , m_fullscreen(m_player, m_settings, m_layout, m_toasts)
    , m_queueRail(m_player, m_library, m_toasts, &m_connectSync)
    , m_ambient(m_player)
    , m_themeBinding(m_settings)
    , m_settingsViewModel(m_settings, m_player, m_mode)
    , m_settingsAbout({.library = m_databaseOpen ? &m_library : nullptr,
                       .links = m_externalLinks,
                       .dataRoot = m_paths.root()})
    , m_source({.mode = m_mode,
                .session = m_session,
                .accountClient = m_accountClient,
                .signOut = m_signOut,
                .links = m_externalLinks,
                .library = m_databaseOpen ? &m_library : nullptr})
    , m_firstRun(m_settings, m_mode, m_navigator)
    , m_profile({.settings = m_settings,
                 .mode = m_mode,
                 .session = m_session,
                 .signOut = m_signOut,
                 .navigator = m_navigator})
    , m_librarySettings({.settings = m_settings,
                         .scanner = m_scanner,
                         .gate = m_userDataGate,
                         .picker = m_folderPicker,
                         .links = m_externalLinks})
    , m_trackMenu(m_trackActions, &m_toasts)
    , m_libraryPage({.library = m_library,
                     .groups = m_groups,
                     .actions = m_trackActions,
                     .collections = m_collectionActions,
                     .mixes = m_homeMixes,
                     .player = m_player,
                     .scanner = m_scanner,
                     .trackMenu = m_trackMenu,
                     .navigator = m_navigator,
                     .mode = &m_mode,
                     .share = &m_share,
                     .download =
                         [this](const QString& title, const QString& subtitle, const QString& artwork,
                                const QList<core::Track>& tracks) {
                             return m_offline.downloadTracks(title, subtitle, artwork, tracks);
                         }})
    , m_catalogArtist({.catalog = m_catalog,
                       .client = m_catalogClient,
                       .accountClient = m_accountClient,
                       .session = m_session,
                       .actions = m_trackActions,
                       .trackMenu = m_trackMenu,
                       .navigator = m_navigator,
                       .share = &m_share,
                       .providerPages =
                           [this]() {
                               return !m_mode.capabilities().browseCatalogue
                                   && m_mode.capabilities().openOnlineArtistPages;
                           },
                       .providerArtist =
                           [this](const core::account::LibraryArtistIdentity& identity) {
                               return m_remote.artistDetail(identity).then(
                                   [identity](const core::Result<QByteArray>& answer) -> core::Result<core::catalog::Detail> {
                                       if (!answer) {
                                           return answer.failure();
                                       }
                                       return core::catalog::parseProviderArtistDetail(answer.value(), identity);
                                   });
                           },
                       .follows = &m_artistFollows})
    , m_lyricsService(
          [this](const core::lyrics::LyricsQuery& query) { return m_remote.lyrics(query); },
          [this]() { return m_mode.capabilities().showLyrics ? m_remote.scopeKey() : QString(); })
    , m_autoplayQueue(m_player)
    , m_autoplay(m_autoplayQueue,
                 {.related =
                      [this](const core::Track& seed) {
                          return m_remote.related(seed).then(&core::catalog::parseProviderTrackListResult);
                      },
                  .search =
                      [this](const QString& query) {
                          // Mixes search only where they may play.
                          if (!m_mode.capabilities().playRadio) {
                              return QtFuture::makeReadyValueFuture(core::Result<QList<core::Track>>(
                                  core::Failure::of(core::FailureKind::NotAvailable, QStringLiteral("No mixes here."))));
                          }
                          return m_remote.search(query).then(&core::catalog::parseProviderTrackListResult);
                      },
                  .history =
                      [this]() {
                          QList<core::Track> recent = m_library.loadHistoryTracks();
                          if (const std::optional<core::home::HomeLibrary> account = accountHomeLibrary()) {
                              recent += account->history;
                          }
                          return recent;
                      }})
    , m_linkImport({.remote = m_remote,
                    .tracks = m_tracks,
                    .collections = m_collections,
                    .gate = m_userDataGate,
                    .accountEditor = &m_accountEditor,
                    .toAccount = [this]() { return m_mode.capabilities().editServerPlaylists; }})
    , m_lyrics(m_player, m_lyricsService)
    , m_offline({.downloads = m_downloads,
                 .actions = m_trackActions,
                 .library = m_library,
                 .trackMenu = m_trackMenu,
                 .toasts = m_toasts,
                 .picker = m_folderPicker,
                 .currentTrack = [this]() { return m_player.currentTrack(); },
                 .canDownload = [this]() { return m_mode.capabilities().downloadSongs; },
                 .stableKey =
                     [this](const core::Track& track) {
                         return core::track_keys::downloadStableKey(m_remote.scopeKeyParts(), track);
                     }})
    , m_audioOutput(m_audioOutputs, m_audio, m_settings)
    , m_share([this](const QByteArray& body) { return sendAccount(core::net::HttpMethod::Post, QStringLiteral("/v1/music/shares"), body); },
              [this]() {
                  return m_mode.capabilities().createShareLinks && m_session.captureOperation().has_value();
              },
              m_toasts)
    , m_collectionPage({.library = m_library,
                        .actions = m_trackActions,
                        .collections = m_collectionActions,
                        .mixes = m_homeMixes,
                        .player = m_player,
                        .settings = m_settings,
                        .trackMenu = m_trackMenu,
                        .navigator = m_navigator,
                        .share = &m_share,
                        .download =
                            [this](const QString& title, const QString& subtitle, const QString& artwork,
                                   const QList<core::Track>& tracks) {
                                return m_offline.downloadTracks(title, subtitle, artwork, tracks);
                            }})
    , m_artistPage({.library = m_library,
                    .actions = m_trackActions,
                    .player = m_player,
                    .trackMenu = m_trackMenu,
                    .navigator = m_navigator,
                    .follows = m_artistFollows,
                    .toasts = m_toasts})
    , m_accountArtists({.client = m_accountClient,
                        .session = m_session,
                        .mode = m_mode,
                        .follows = m_artistFollows,
                        .navigator = m_navigator,
                        .toasts = &m_toasts})
    , m_welcome({.client = m_accountClient,
                 .session = m_session,
                 .sync = m_sync,
                 .accounts = m_accounts,
                 .settings = m_settings})
    , m_connectivity(m_session, m_mode)
    , m_regionLanguages({.catalog = m_catalog,
                         .client = m_catalogClient,
                         .accountClient = m_accountClient,
                         .session = m_session,
                         .settings = m_settings,
                         .clock = m_clock,
                         .toasts = m_toasts})
    , m_catalogPage({.catalog = m_catalog,
                     .editor = m_accountEditor,
                     .actions = m_trackActions,
                     .trackMenu = m_trackMenu,
                     .navigator = m_navigator,
                     .share = &m_share,
                     .download =
                         [this](const QString& title, const QString& subtitle, const QString& artwork,
                                const QList<core::Track>& tracks) {
                             return m_offline.downloadTracks(title, subtitle, artwork, tracks);
                         },
                     .providerPages =
                         [this]() {
                             return !m_mode.capabilities().browseCatalogue && m_mode.capabilities().openOnlineAlbumPages;
                         },
                     .providerAlbum =
                         [this](const core::account::LibraryArtistIdentity& identity, const QString& artist) {
                             return m_remote.albumDetail(identity, artist)
                                 .then([identity](const core::Result<QByteArray>& answer) -> core::Result<core::catalog::Detail> {
                                     if (!answer) {
                                         return answer.failure();
                                     }
                                     return core::catalog::parseProviderAlbumDetail(answer.value(), identity);
                                 });
                         }})
    , m_collectionEditor(m_collectionActions, m_navigator, m_toasts)
    , m_home({.library = m_library,
              .actions = m_trackActions,
              .collections = m_collectionActions,
              .player = m_player,
              .scanner = m_scanner,
              .mixes = m_homeMixes,
              .trackMenu = m_trackMenu,
              .navigator = m_navigator,
              .now = [] { return QDateTime::currentDateTime(); },
              .catalog = &m_catalog,
              .mode = &m_mode,
              .sync = &m_sync,
              .session = &m_session,
              .share = &m_share,
              .download =
                  [this](const QString& title, const QString& subtitle, const QString& artwork,
                         const QList<core::Track>& tracks) {
                      return m_offline.downloadTracks(title, subtitle, artwork, tracks);
                  }})
    , m_localSearch(m_library, m_groups)
    , m_search({.source = m_localSearch,
                .library = m_library,
                .groups = m_groups,
                .actions = m_trackActions,
                .player = m_player,
                .settings = m_settings,
                .trackMenu = m_trackMenu,
                .navigator = m_navigator,
                .accountLibrary = [this]() { return accountHomeLibrary(); },
                .catalog = &m_catalog,
                .online = [this]() { return m_mode.capabilities().browseCatalogue; },
                .shareCode =
                    [this](const QString& text) {
                        return core::share::shareCodeFromLink(text, m_privateConfig.accountFrontendOrigin);
                    },
                .remoteSongs =
                    [this](const QString& query) -> QFuture<core::Result<QList<core::Track>>> {
                        // The catalogue's own search covers Account mode.
                        if (!m_mode.capabilities().searchOnline || m_mode.capabilities().browseCatalogue) {
                            return QtFuture::makeReadyValueFuture(core::Result<QList<core::Track>>(QList<core::Track>()));
                        }
                        return m_remote.search(query).then(&core::catalog::parseProviderTrackListResult);
                    }})
    , m_cleanup({.gate = m_userDataGate,
                 .scanner = m_scanner,
                 .player = m_player,
                 .tracks = m_tracks,
                 .settings = m_settings,
                 .secrets = m_secrets,
                 .session = m_session,
                 .mode = m_mode,
                 .stateFile = m_paths.stateFile(),
                 .coverFolder = m_paths.thumbsFolder()})
    , m_dataSettings(m_cleanup, m_toasts)
    , m_appDataReset({.signOut = m_signOut,
                      .session = m_session,
                      .player = m_player,
                      .settings = m_settings,
                      .streamCache = m_streamCache,
                      .artwork = m_artwork})
    , m_dataPrivacy({.client = m_accountClient, .session = m_session, .sync = m_sync, .reset = m_appDataReset})
    , m_placementKeeper(m_windowPlacement, m_settings)
    , m_closeToTray(m_tray,
                    m_settings,
                    {.togglePlay = [this]() { m_player.togglePlay(); },
                     .next = [this]() { m_player.next(); },
                     .previous = [this]() { m_player.previous(); }})
    , m_presence(m_privateConfig.discordClientId)
    , m_cast({.browser = m_dnsBrowser,
              .player = m_player,
              .accountResolver = m_accountResolver,
              .castAllowed = [this]() { return m_mode.capabilities().castToDevices; },
              .apiKeyMode = [this]() { return m_mode.apiKeySelected(); },
              .makeTransport = []() -> std::unique_ptr<core::cast::ICastTransport> {
                  return std::make_unique<core::cast::SslCastTransport>();
              }})
    , m_castViewModel(m_cast, &m_connectClient, &m_connectSync)
    , m_equalizer(m_settings)
    , m_historyImport({.gate = m_userDataGate,
                       .accountId = [this]() {
                           return m_mode.capabilities().importLocalHistory
                                       && m_session.state() == core::account::AccountSessionState::Validated
                                   ? m_session.profile().id
                                   : QString();
                       },
                       .loadState = [this](const QString& owner) { return m_accounts.loadAccountSyncState(owner); },
                       .saveState =
                           [this](const core::db::AccountSyncState& state) {
                               return static_cast<bool>(m_accounts.saveAccountSyncState(state));
                           },
                       .candidates = [this]() { return m_library.loadLegacyRemoteHistoryImportCandidates(); },
                       .resolve = [this](const QString& url) { return m_remote.resolve(url); },
                       .enqueue =
                           [this](const core::db::PlaybackEventRecord& event) {
                               return static_cast<bool>(m_outbox.enqueuePlaybackEvent(event));
                           },
                       .sendHistory =
                           [this]() { (void)m_sync.requestSync(core::account::MusicSyncService::Lane::History); }})
    , m_historyImportViewModel(m_historyImport)
    , m_appRoot({
          .windowChrome = &m_windowChromeViewModel,
          .navigator = &m_navigator,
          .layout = &m_layout,
          .sidebar = &m_sidebar,
          .playerBar = &m_playerBar,
          .fullscreen = &m_fullscreen,
          .queueRail = &m_queueRail,
          .toasts = &m_toasts,
          .settings = &m_settingsViewModel,
          .settingsAbout = &m_settingsAbout,
          .source = &m_source,
          .firstRun = &m_firstRun,
          .profile = &m_profile,
          .librarySettings = &m_librarySettings,
          .library = &m_libraryPage,
          .trackMenu = &m_trackMenu,
          .collection = &m_collectionPage,
          .localArtist = &m_artistPage,
          .collections = &m_collectionEditor,
          .home = &m_home,
          .search = &m_search,
          .dataSettings = &m_dataSettings,
        .dataPrivacy = &m_dataPrivacy,
          .accountArtists = &m_accountArtists,
          .welcome = &m_welcome,
          .connectivity = &m_connectivity,
          .regionLanguages = &m_regionLanguages,
          .catalogPage = &m_catalogPage,
          .catalogArtist = &m_catalogArtist,
          .lyrics = &m_lyrics,
          .offline = &m_offline,
          .audioOutput = &m_audioOutput,
          .presenceAvailable = m_presence.available(),
          .cast = &m_castViewModel,
          .coverFlight = &m_coverFlight,
          .cascade = &m_pageCascade,
          .effects = &m_effects,
          .focusPlace = &m_focusPlace,
          .equalizer = &m_equalizer,
          .historyImport = &m_historyImportViewModel,
          .share = &m_share,
          .mode = &m_mode,
          .showGallery = QCoreApplication::arguments().contains(QStringLiteral("--gallery")),
      })
{
    // The song menu's Download, Remove download and Retry.
    m_trackMenu.setDownloadHooks(
        {.state = [this](const core::Track& track) { return m_offline.songDownloadState(track); },
         .run = [this](const core::Track& track, const QString& action) { m_offline.runSongDownload(track, action); }});
    // Before any window exists, so the taskbar and the media session use it.
    // A failure is logged inside and the app runs on.
    [[maybe_unused]] const bool identitySet =
        platform::windows::WindowsAppIdentity::apply(core::AppIdentity::appUserModelId());
    ui::AppRoot::registerInstance(&m_appRoot);

    // In Account mode the mixes come from the account library, read as the
    // reference client's home model reads it.
    m_homeMixes.setAccountSource([this]() { return accountHomeLibrary(); });
    // A song menu's Go to album: an account song's album comes from the
    // catalogue (the library's own albums open in the library otherwise).
    QObject::connect(&m_trackActions, &core::TrackActions::albumRequested, &m_catalogPage, [this](const core::Track& track) {
        switch (m_trackActions.albumPageSource(track)) {
        case core::TrackActions::PageSource::Catalogue:
            m_catalogPage.openAlbumOf(track);
            break;
        case core::TrackActions::PageSource::Provider:
            m_catalogPage.openProviderAlbumOf(track);
            break;
        case core::TrackActions::PageSource::Library:
            break;
        }
    });
    m_collectionEditor.setLinkImporter(&m_linkImport);
    QObject::connect(&m_linkImport, &core::LinkImportService::imported, &m_collectionActions,
                     [this](const QString& kind) {
                         if (kind == QLatin1StringView("playlist")) {
                             emit m_collectionActions.playlistsChanged();
                         } else {
                             emit m_collectionActions.albumsChanged();
                         }
                     });
    // Offline downloads: a finished one plays first; liked and recently
    // played songs are fetched when the rules say so; the rules are checked
    // again every few seconds and on every connection change.
    m_downloads.initialize();
    m_resolver.setDownloadLookup([this](const core::Track& track) {
        return m_downloads.readyPath(core::track_keys::downloadStableKey(m_remote.scopeKeyParts(), track));
    });
    const auto autoDownload = [this](const core::Track& track, const QString& reason) {
        if (!m_mode.capabilities().downloadSongs || !track.isRemote() || !track.sourceUrl.startsWith(QLatin1StringView("http"))) {
            return;
        }
        const QString key = core::track_keys::downloadStableKey(m_remote.scopeKeyParts(), track);
        (void)m_downloads.enqueue(track.title, reason, track.artworkUrl,
                                  {{key, track.sourceUrl, track.sourceTag, track.title, track.artist, track.album,
                                    track.artworkUrl}});
    };
    QObject::connect(&m_trackActions, &core::TrackActions::songLiked, &m_downloads, [this, autoDownload](const core::Track& track) {
        if (m_downloads.snapshot().autoDownloadLiked) {
            autoDownload(track, QStringLiteral("Liked song"));
        }
    });
    QObject::connect(&m_player, &core::PlayerController::playRecorded, &m_downloads, [this, autoDownload](const core::Track& track) {
        if (m_downloads.snapshot().keepRecentOffline) {
            autoDownload(track, QStringLiteral("Recently played"));
        }
    });
    m_downloadRules.setInterval(3000);
    QObject::connect(&m_downloadRules, &QTimer::timeout, &m_downloads, [this]() { m_downloads.refreshScheduling(); });
    m_downloadRules.start();
    // Casting ends when the mode no longer allows it; its messages are toasts.
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_cast, [this]() { m_cast.refreshAllowed(); });
    QObject::connect(&m_cast, &core::cast::CastController::notice, &m_toasts,
                     [this](const QString& text) { m_toasts.show(text); });
    // After a clean sync, the one-time offer to import this PC's history.
    QObject::connect(&m_sync, &core::account::MusicSyncService::syncingChanged, &m_historyImport, [this]() {
        if (!m_sync.isSyncing() && m_sync.lastSafeError().isEmpty()) {
            m_historyImport.offerIfDue();
        }
    });
    QObject::connect(&m_historyImportViewModel, &ui::HistoryImportViewModel::message, &m_toasts,
                     [this](const QString& text) { m_toasts.show(text); });
    // Each page opened starts the cascade of its cards and rows.
    QObject::connect(&m_navigator, &ui::Navigator::currentChanged, &m_pageCascade, [this]() { m_pageCascade.restart(); });
    // A song sent to the queue from a press arcs into it.
    QObject::connect(&m_trackActions, &core::TrackActions::queued, &m_effects,
                     [this](const core::Track& track) { m_effects.trackQueued(track); });
    // The device link runs while the mode allows it (a confirmed account
    // session and a network); a casting change is reported to the others.
    const auto updateConnect = [this]() {
        if (m_mode.capabilities().controlOtherDevices) {
            m_connectClient.start();
        } else {
            m_connectClient.stop();
        }
    };
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_connectClient, updateConnect);
    // Another account signed in: the link is that account's from now on.
    QObject::connect(&m_session, &core::account::AccountSessionService::ownerChanged, &m_connectClient,
                     [this, updateConnect]() {
                         m_connectClient.stop();
                         updateConnect();
                     });
    QObject::connect(&m_connectSync, &core::connect::ConnectSync::notice, &m_toasts,
                     [this](const QString& text) { m_toasts.show(text, true); });
    updateConnect();
    // A queue row's menu also offers the song menu's items.
    m_queueRail.setTrackMenu(&m_trackMenu);
    // Discord presence follows its setting and the player.
    m_presence.setEnabled(m_settings.boolean(core::settings::keys::DiscordPresence, false));
    m_presenceVisibilityTimer.setSingleShot(true);
    QObject::connect(&m_presenceVisibilityTimer, &QTimer::timeout, &m_presence, [this]() { updatePresence(); });
    QObject::connect(&m_cast, &core::cast::CastController::phaseChanged, &m_presence, [this]() { updatePresence(); });
    QObject::connect(&m_settings, &core::SettingsStore::changed, &m_presence, [this](const QString& key) {
        namespace keys = core::settings::keys;
        const bool cardSetting = key == keys::DiscordShowSong || key == keys::DiscordLayout
            || key == keys::DiscordWhenPaused || key == keys::DiscordShowCastDevice || key == keys::DiscordSnoozeUntil;
        if (cardSetting) {
            updatePresence();
            return;
        }
        if (key.isEmpty() || key == keys::DiscordPresence) {
            m_presence.setEnabled(m_settings.boolean(keys::DiscordPresence, false));
            updatePresence();
        }
    });
    QObject::connect(&m_player, &core::PlayerController::currentTrackChanged, &m_presence, [this]() { updatePresence(); });
    QObject::connect(&m_player, &core::PlayerController::stateChanged, &m_presence, [this]() { updatePresence(); });
    QObject::connect(&m_player, &core::PlayerController::durationChanged, &m_presence, [this]() { updatePresence(); });
    // API key mode: the local mixes are filled from provider searches.
    m_homeMixes.setRemoteFill(
        [this](const QString& query) { return m_remote.search(query).then(&core::catalog::parseProviderTrackListResult); },
        [this]() { return m_mode.capabilities().showOnlineMixes && !m_mode.capabilities().browseCatalogue; });
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_homeMixes, [this]() { m_homeMixes.refresh(true); });
    // Autoplay follows the player's queue; a mix that finds nothing says so.
    QObject::connect(&m_player, &core::PlayerController::autoplayExtensionWanted, &m_autoplay,
                     [this](bool queueEnded) { m_autoplay.extensionWanted(queueEnded); });
    QObject::connect(&m_player, &core::PlayerController::contextReplaced, &m_autoplay,
                     [this]() { m_autoplay.contextReplaced(); });
    QObject::connect(&m_player, &core::PlayerController::autoplayChanged, &m_autoplay,
                     [this]() { m_autoplay.invalidate(); });
    QObject::connect(&m_player, &core::PlayerController::repeatModeChanged, &m_autoplay,
                     [this]() { m_autoplay.invalidate(); });
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_autoplay, [this]() { m_autoplay.invalidate(); });
    QObject::connect(&m_trackActions, &core::TrackActions::mixRequested, &m_autoplay,
                     [this](const core::Track& track) { m_autoplay.startMix(track); });
    QObject::connect(&m_autoplay, &core::AutoplayService::mixFoundNothing, &m_toasts,
                     [this]() { m_toasts.show(QCoreApplication::translate("Autoplay", "No similar songs found for this radio.")); });
    // Share links: a song from its menu or the player bar.
    QObject::connect(&m_trackActions, &core::TrackActions::shareRequested, &m_share, [this](const core::Track& track) {
        m_share.share({.kind = core::share::ShareKind::Song, .track = track},
                      QCoreApplication::translate("Share", "Could not share this song."));
    });
    QObject::connect(&m_playerBar, &ui::PlayerBarViewModel::shareRequested, &m_share, [this]() {
        if (const std::optional<core::Track>& track = m_player.currentTrack()) {
            m_trackActions.share(*track);
        }
    });
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_share, [this]() { m_share.refresh(); });
    QObject::connect(&m_session, &core::account::AccountSessionService::stateChanged, &m_share,
                     [this]() { m_share.refresh(); });
    // Another account, key or mode asks in another scope.
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_lyrics, [this]() { m_lyrics.reload(); });
    QObject::connect(&m_session, &core::account::AccountSessionService::ownerChanged, &m_lyrics,
                     [this]() { m_lyrics.reload(); });
    QObject::connect(&m_trackActions, &core::TrackActions::artistRequested, &m_catalogArtist,
                     [this](const core::Track& track) {
                         if (m_trackActions.artistPageSource(track) != core::TrackActions::PageSource::Library) {
                             m_catalogArtist.openArtistOf(track);
                         }
                     });

    // Whether this is a first run, read before the mode service stores its
    // first-run default.
    core::mode::SourceChoiceInputs sourceChoice{
        .choiceSeenStored = m_settings.contains(core::settings::keys::SourceChoiceSeen),
        .choiceSeen = m_settings.boolean(core::settings::keys::SourceChoiceSeen, false),
        .modeStoredBefore = !m_settings.string(core::settings::keys::RemoteAccessMode).isEmpty(),
        .libraryHasSongs = m_databaseOpen && m_library.libraryStats().songCount > 0,
        .sessionStored = storedSessionExists(m_secrets),
    };

    // A sign-out that could not clear its data finishes now, unless the same
    // account signed in again (decided once the stored session was checked).
    if (!m_settings.string(core::settings::keys::PendingAccountCleanupOwnerId).isEmpty()) {
        const auto resolve = [this]() { (void)m_signOut.resolvePendingCleanup(); };
        if (storedSessionExists(m_secrets)) {
            QObject::connect(&m_session, &core::account::AccountSessionService::stateChanged, &m_signOut, resolve,
                             Qt::SingleShotConnection);
        } else {
            QMetaObject::invokeMethod(&m_signOut, resolve, Qt::QueuedConnection);
        }
    }

    // Reads the stored music source (writing the first-run default) and
    // sets the library's active context. A plaintext key left by an earlier
    // version moves into the credential store here.
    [[maybe_unused]] const core::account::LegacyKeyMigration migration = m_mode.start();
    // As before, leftovers are pruned only when the app starts in API key mode.
    if (m_mode.apiKeySelected()) {
        m_streamCache.pruneOnStartup();
    }
    // A new source never reuses transfers started for the old one.
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_streamCache,
                     [this]() { m_streamCache.invalidateInFlight(); });
    sourceChoice.mode = m_mode.mode();
    m_firstRun.decide(sourceChoice);
    // Checks a stored account session with the service (or falls back to
    // the saved profile while offline); without one it stays signed out.
    m_session.restore();

    // Songs whose files were removed since the last run leave the library
    // (marked inactive, never deleted). The files are checked on a worker.
    if (m_databaseOpen) {
        [[maybe_unused]] const bool pruning = m_scanner.pruneMissingTracks();
        m_sidebar.refreshPlaylists();
    }

    // A signed-out account's songs leave the player and the views.
    QObject::connect(&m_signOut, &core::account::AccountDataLifecycleService::accountDataRemoved, &m_signOut,
                     [this](const QString&) {
                         const auto isAccountSong = [](const core::Track& track) {
                             return track.origin == QLatin1StringView("account");
                         };
                         if (m_player.currentTrack() && isAccountSong(*m_player.currentTrack())) {
                             m_player.reset();
                         } else {
                             const QList<core::QueueEntry> upcoming = m_player.visibleQueue();
                             for (qsizetype index = upcoming.size() - 1; index >= 0; --index) {
                                 if (isAccountSong(upcoming.at(index).track)) {
                                     m_player.removeVisibleEntry(index);
                                 }
                             }
                         }
                         refreshLibraryViews();
                     });
    QObject::connect(&m_signOut, &core::account::AccountDataLifecycleService::signOutFinished, &m_toasts,
                     [this](const core::account::AccountLogoutResult& result) {
                         if (!result.safeError.isEmpty()) {
                             m_toasts.show(result.safeError, !result.signedOut);
                         }
                     });
    // Account songs and playlists are kept on the account in Account mode.
    m_collectionActions.setAccountEditor(&m_accountEditor);
    m_player.setAccountPlaybackSink(&m_accountEditor);
    QObject::connect(&m_accountEditor, &core::account::AccountLibraryEditor::failed, &m_toasts,
                     [this](const QString& message) { m_toasts.show(message, true); });

    // After "Delete all data" every view reads its (now empty) data again.
    QObject::connect(&m_cleanup, &core::UserDataCleanup::cleared, &m_cleanup, [this]() {
        refreshLibraryViews();
        m_settingsAbout.refresh();
        m_navigator.openView(static_cast<int>(ui::Route::Kind::Home));
    });
    // A synced account library, or a change of music source, changes what
    // the library holds.
    QObject::connect(&m_sync, &core::account::MusicSyncService::libraryChanged, &m_sync,
                     [this]() { refreshLibraryViews(); });
    QObject::connect(&m_mode, &core::mode::ModeService::changed, &m_mode,
                     [this, shown = m_mode.mode()]() mutable {
                         if (m_mode.mode() != shown) {
                             shown = m_mode.mode();
                             refreshLibraryViews();
                         }
                     });
    // The window's frame is kept for the next run.
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, &m_placementKeeper,
                     &WindowPlacementKeeper::save);

    // The last song comes back into the player bar once the state file is
    // read (on the file's worker); the first Play opens it.
    m_persistence.restoreAtLaunch();
}

void CompositionRoot::showWelcomeForReview()
{
    m_welcome.open(false);
}

void CompositionRoot::showViewForReview(const QString& view)
{
    const std::optional<ui::Route::Kind> kind = view == QLatin1StringView("library") ? std::optional(ui::Route::Kind::Library)
        : view == QLatin1StringView("settings")                                     ? std::optional(ui::Route::Kind::Settings)
        : view == QLatin1StringView("offline")                                      ? std::optional(ui::Route::Kind::Offline)
                                                                                    : std::nullopt;
    if (kind) {
        m_navigator.openView(static_cast<int>(*kind));
    }
}

void CompositionRoot::showSearchForReview(const QString& query)
{
    m_navigator.openView(static_cast<int>(ui::Route::Kind::Browse));
    m_search.setQuery(query);
}

QFuture<core::Result<QByteArray>> CompositionRoot::sendAccount(core::net::HttpMethod method, const QString& pathAndQuery,
                                                               const QByteArray& jsonBody)
{
    const std::optional<core::account::AccountOperationContext> context = m_session.captureOperation();
    if (!context) {
        return QtFuture::makeReadyValueFuture(
            core::Result<QByteArray>(core::Failure::of(core::FailureKind::Unauthorized, QStringLiteral("Not signed in."))));
    }
    core::account::AccountCall call;
    call.method = method;
    call.pathAndQuery = pathAndQuery;
    call.bearerSession = context->bearerSession;
    call.jsonBody = jsonBody;
    return m_accountClient.send(call).then(
        [this, context = *context](const core::Result<core::net::HttpResponse>& response) -> core::Result<QByteArray> {
            if (!response) {
                if (response.failure().kind == core::FailureKind::Unauthorized) {
                    m_session.handleUnauthorized(context);
                }
                return response.failure();
            }
            return response.value().body;
        });
}

void CompositionRoot::updatePresence()
{
    m_presenceVisibilityTimer.stop();
    const std::optional<core::Track>& track = m_player.currentTrack();
    if (!track || m_player.state() == core::PlayerController::State::Idle) {
        m_presencePausedSince = 0;
        m_presence.clear();
        return;
    }
    const bool playing = m_player.state() == core::PlayerController::State::Playing;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (playing) {
        m_presencePausedSince = 0;
    } else if (m_presencePausedSince == 0) {
        m_presencePausedSince = now;
    }
    // A pause that runs out its delay, or a sharing pause that ends, is
    // looked at again then.
    const core::presence::PresenceVisibility visibility = core::presence::presenceVisibilityFrom(m_settings);
    if (const std::optional<qint64> next =
            core::presence::nextVisibilityChange(visibility, m_presencePausedSince, now)) {
        m_presenceVisibilityTimer.start(std::chrono::seconds(*next - now));
    }
    if (!core::presence::shouldPublish(visibility, m_presencePausedSince, now)) {
        m_presence.clear();
        return;
    }

    const core::presence::PresenceOptions options = core::presence::presenceOptionsFrom(m_settings);
    const QString castDevice =
        m_cast.phase() == core::cast::CastController::Phase::Casting ? m_cast.deviceName() : QString();
    const core::presence::PresenceLines lines =
        core::presence::presenceLines(core::displayTitle(track->title), track->artist, track->album, options, castDevice);
    core::presence::PresencePayload payload;
    payload.details = lines.details;
    payload.state = lines.state;
    payload.durationSeconds = m_player.durationSeconds() > 0 ? m_player.durationSeconds() : track->durationSeconds;
    payload.positionSeconds = m_player.positionSeconds();
    payload.isPlaying = playing;
    payload.isLocal = !track->isRemote();
    const QString songKey = track->title + QLatin1Char('\n') + track->artist;
    if (lines.showArtwork) {
        // A public https picture or an account cover link on the account
        // service; API key mode asks the provider for one Discord can show,
        // as the earlier versions did.
        payload.artworkUrl = core::presence::presenceArtworkUrl(track->artworkUrl, m_accountClient.baseOrigin());
        if (m_presenceArtworkFor == songKey && !m_presenceArtwork.isEmpty()) {
            payload.artworkUrl = m_presenceArtwork;
        }
    }
    m_presence.show(payload);
    if (!lines.showArtwork || m_presenceArtworkFor == songKey || !m_mode.capabilities().lookUpPresenceArtwork
        || !m_settings.boolean(core::settings::keys::DiscordPresence, false)) {
        return;
    }
    m_presenceArtworkFor = songKey;
    m_presenceArtwork.clear();
    const std::optional<core::provider::ProviderConnection> connection = m_mode.providerConnection();
    if (!connection) {
        return;
    }
    m_providerClient.presenceArtwork(*connection, track->title, track->artist)
        .then(&m_presence, [this, songKey](const core::Result<QByteArray>& answer) {
            if (!answer || m_presenceArtworkFor != songKey) {
                return;
            }
            const QString url =
                QJsonDocument::fromJson(answer.value()).object().value(QLatin1StringView("url")).toString();
            if (url.isEmpty()) {
                return;
            }
            m_presenceArtwork = url;
            updatePresence();
        });
}

std::optional<core::home::HomeLibrary> CompositionRoot::accountHomeLibrary() const
{
    if (!m_mode.accountSelected()) {
        return std::nullopt;
    }
    const QString owner = m_session.profile().id;
    const std::optional<core::db::AccountLibrarySnapshot> snapshot =
        owner.isEmpty() ? std::nullopt : m_accounts.loadAccountLibrary(owner);
    return snapshot ? std::optional(core::home::homeLibraryFromSnapshot(*snapshot)) : std::nullopt;
}

void CompositionRoot::refreshLibraryViews()
{
    m_homeMixes.refresh(true);
    m_sidebar.refreshPlaylists();
    m_libraryPage.refresh();
    m_home.refresh();
}

CompositionRoot::~CompositionRoot()
{
    ui::AppRoot::registerInstance(nullptr);
    // QML is gone by now (the engine is destroyed first). Stop the sampling
    // worker, then save where playback stood and wait for the file, then
    // the pending volume, while every service still exists. The device link
    // goes first, so nothing is reported while the rest stops.
    m_connectClient.stop();
    m_cast.shutdown();
    m_ambient.shutdown();
    m_persistence.shutdown();
    m_player.shutdown();
    m_downloads.shutdown();
}

bool CompositionRoot::claimSingleInstance()
{
    if (m_singleInstance->tryAcquire()) {
        return true;
    }
    m_singleInstance->activateExistingWindow();
    return false;
}

bool CompositionRoot::showMainWindow(QQmlApplicationEngine& engine)
{
    const QList<QObject*> roots = engine.rootObjects();
    QWindow* window = roots.isEmpty() ? nullptr : qobject_cast<QWindow*>(roots.first());
    if (window == nullptr) {
        return false;
    }
    // winId() creates the native window without showing it, so the custom
    // frame is in place before the first frame is drawn.
    const bool custom = m_windowChrome.attach(static_cast<quintptr>(window->winId()));
    if (!custom) {
        qCWarning(lcComposition) << "Keeping the system window frame";
    }
    m_windowChromeViewModel.setAttached(custom);
    // The folder picker is modal to the main window.
    m_librarySettings.setOwnerWindow(static_cast<quintptr>(window->winId()));
    m_offline.setOwnerWindow(static_cast<quintptr>(window->winId()));
    m_closeToTray.attach(window);
    // Looping animations stop while the window is minimised or in the tray.
    QObject::connect(window, &QWindow::visibilityChanged, window, [this](QWindow::Visibility visibility) {
        if (m_motion) {
            m_motion->setWindowShown(visibility != QWindow::Minimized && visibility != QWindow::Hidden);
        }
    });
    // Presses, for where the queue arc starts.
    window->installEventFilter(&m_effects);
    // The system routes the media keys to the session of this window.
    if (!m_mediaControls.attach(static_cast<quintptr>(window->winId()))) {
        qCWarning(lcComposition) << "The system media controls are not available";
    }
    if (m_theme) {
        // The frame follows the theme: dark frame and backdrop in dark mode.
        m_windowChromeViewModel.applyTheme(m_theme->dark());
        QObject::connect(m_theme, &ui::ThemeColors::changed, &m_windowChromeViewModel,
                         [this]() {
                             if (m_theme) {
                                 m_windowChromeViewModel.applyTheme(m_theme->dark());
                             }
                         });
    }
    // The size and place it had when it last closed, in physical pixels;
    // the place only while a monitor still shows it.
    m_placementKeeper.attach(*window);
    // Coming back to the window may be a reason to check the account library.
    QObject::connect(window, &QWindow::activeChanged, &m_syncScheduler, [this, window]() {
        if (window->isActive()) {
            m_syncScheduler.windowActivated();
        }
    });
    window->show();
    return true;
}

void CompositionRoot::attachEngine(QQmlEngine& engine)
{
    // Before the theme resolves its font families.
    ui::FontLoader::loadBundledFonts();

    m_theme = engine.singletonInstance<ui::Theme*>("LastMusicPlayer", "Theme");
    // The saved theme and accent, before the first frame.
    m_themeBinding.attach(m_theme);
    m_ambient.setTheme(m_theme);
    // The engine takes ownership of the provider.
    engine.addImageProvider(QString(ui::CoverSource::ProviderId),
                            std::make_unique<ui::EmbeddedCoverImageProvider>().release());
    engine.addImageProvider(QString(ui::CoverSource::RemoteProviderId),
                            std::make_unique<ui::ArtworkImageProvider>(m_artwork).release());
    engine.addImageProvider(QStringLiteral("chartart"), std::make_unique<ui::ChartArtImageProvider>().release());

    m_reducedMotion = std::make_unique<platform::windows::WindowsReducedMotion>();
    QPointer<ui::Motion> motion =
        engine.singletonInstance<ui::Motion*>("LastMusicPlayer", "Motion");
    if (motion.isNull()) {
        return;
    }
    m_motion = motion;
    motion->setReduced(m_reducedMotion->reduced());
    // The engine owns Motion and is destroyed before this root, so the
    // guarded pointer is checked on every change.
    m_reducedMotion->setChangeHandler([motion](bool reduced) {
        if (motion) {
            motion->setReduced(reduced);
        }
    });
}

} // namespace lmp::app
