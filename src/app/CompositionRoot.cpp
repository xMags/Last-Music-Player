#include "app/CompositionRoot.h"

#include <QCoreApplication>
#include <QList>
#include <QLoggingCategory>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
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

} // namespace

CompositionRoot::CompositionRoot()
    : m_singleInstance(std::make_unique<platform::windows::WindowsSingleInstance>(
          core::AppIdentity::singleInstanceMutexName(), core::AppIdentity::displayName()))
    , m_paths(core::AppPaths::forCurrentUser())
    , m_settings(m_paths.settingsFile())
    , m_database(m_paths)
    , m_databaseOpen(openDatabase(m_database))
    , m_tracks(m_database)
    , m_library(m_database)
    , m_collections(m_database)
    , m_groups(m_database)
    , m_artworkCache(m_paths.thumbsFolder(), &m_tagReader)
    , m_scanner({.tags = m_tagReader,
                 .artwork = m_artworkCache,
                 .tracks = m_tracks,
                 .library = m_library,
                 .settings = m_settings,
                 .gate = m_userDataGate})
    , m_player({.audio = m_audio, .settings = m_settings, .tracks = m_tracks, .resolver = m_resolver, .clock = m_clock},
               core::QueueEngine())
    , m_persistence({.player = m_player,
                     .settings = m_settings,
                     .tracks = m_tracks,
                     .library = m_library,
                     .userDataGate = m_userDataGate,
                     .clock = m_clock},
                    m_paths)
    , m_mediaBridge({.player = m_player, .controls = m_mediaControls, .settings = m_settings, .clock = m_clock})
    , m_externalLinks(m_paths.root())
    , m_secrets(m_credentials, m_settings)
    , m_privateConfig(core::PrivateConfig::fromBuild())
    , m_accountClient(m_http, m_privateConfig)
    , m_session({.client = m_accountClient,
                 .secrets = m_secrets,
                 .settings = m_settings,
                 .links = m_externalLinks,
                 .operationGate = m_userDataGate})
    , m_providerClient(m_http)
    , m_accounts(m_database)
    , m_mode({.settings = m_settings,
              .secrets = m_secrets,
              .session = m_session,
              .provider = m_providerClient,
              .accounts = m_accounts,
              .operationGate = m_userDataGate})
    , m_signOut(m_session)
    , m_trackActions({.player = m_player,
                      .tracks = m_tracks,
                      .collections = m_collections,
                      .groups = m_groups,
                      .mode = m_mode,
                      .gate = m_userDataGate})
    , m_windowChromeViewModel(&m_windowChrome)
    , m_sidebar(&m_navigator)
    , m_playerBar(m_player)
    , m_fullscreen(m_player, m_settings, m_layout, m_toasts)
    , m_queueRail(m_player, m_library, m_toasts)
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
    , m_trackMenu(m_trackActions)
    , m_libraryPage({.library = m_library,
                     .groups = m_groups,
                     .actions = m_trackActions,
                     .player = m_player,
                     .scanner = m_scanner,
                     .trackMenu = m_trackMenu,
                     .navigator = m_navigator})
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
          .mode = &m_mode,
          .showGallery = QCoreApplication::arguments().contains(QStringLiteral("--gallery")),
      })
{
    // Before any window exists, so the taskbar and the media session use it.
    // A failure is logged inside and the app runs on.
    [[maybe_unused]] const bool identitySet =
        platform::windows::WindowsAppIdentity::apply(core::AppIdentity::appUserModelId());
    ui::AppRoot::registerInstance(&m_appRoot);

    // Whether this is a first run, read before the mode service stores its
    // first-run default.
    core::mode::SourceChoiceInputs sourceChoice{
        .choiceSeenStored = m_settings.contains(core::settings::keys::SourceChoiceSeen),
        .choiceSeen = m_settings.boolean(core::settings::keys::SourceChoiceSeen, false),
        .modeStoredBefore = !m_settings.string(core::settings::keys::RemoteAccessMode).isEmpty(),
        .libraryHasSongs = m_databaseOpen && m_library.libraryStats().songCount > 0,
        .sessionStored = storedSessionExists(m_secrets),
    };

    // Reads the stored music source (writing the first-run default) and
    // sets the library's active context. A plaintext key left by an earlier
    // version moves into the credential store here.
    [[maybe_unused]] const core::account::LegacyKeyMigration migration = m_mode.start();
    sourceChoice.mode = m_mode.mode();
    m_firstRun.decide(sourceChoice);
    // Checks a stored account session with the service (or falls back to
    // the saved profile while offline); without one it stays signed out.
    m_session.restore();

    // Songs whose files were removed since the last run leave the library
    // (marked inactive, never deleted). The files are checked on a worker.
    if (m_databaseOpen) {
        [[maybe_unused]] const bool pruning = m_scanner.pruneMissingTracks();
    }

    // The last song comes back into the player bar once the state file is
    // read (on the file's worker); the first Play opens it.
    m_persistence.restoreAtLaunch();
}

CompositionRoot::~CompositionRoot()
{
    ui::AppRoot::registerInstance(nullptr);
    // QML is gone by now (the engine is destroyed first). Stop the sampling
    // worker, then save where playback stood and wait for the file, then
    // the pending volume, while every service still exists.
    m_ambient.shutdown();
    m_persistence.shutdown();
    m_player.shutdown();
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

    m_reducedMotion = std::make_unique<platform::windows::WindowsReducedMotion>();
    QPointer<ui::Motion> motion =
        engine.singletonInstance<ui::Motion*>("LastMusicPlayer", "Motion");
    if (motion.isNull()) {
        return;
    }
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
