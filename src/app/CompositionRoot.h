#pragma once

#include <QPointer>

#include <memory>

#include "core/app/AppPaths.h"
#include "core/db/Database.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/playback/MediaControlsBridge.h"
#include "core/playback/PlaybackClock.h"
#include "core/playback/PlaybackStatePersistence.h"
#include "core/playback/PlayerController.h"
#include "core/playback/TrackSourceResolver.h"
#include "core/settings/SettingsStore.h"
#include "platform/ISingleInstance.h"
#include "platform/windows/WindowsAudioPlayer.h"
#include "platform/windows/WindowsMediaControls.h"
#include "platform/windows/WindowsReducedMotion.h"
#include "platform/windows/WindowsWindowChrome.h"
#include "ui/navigation/Navigator.h"
#include "ui/player/AmbientAccentSampler.h"
#include "ui/player/FullscreenPlayerViewModel.h"
#include "ui/player/PlayerBarViewModel.h"
#include "ui/player/QueueRailViewModel.h"
#include "ui/shell/AppRoot.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/shell/SidebarViewModel.h"
#include "ui/shell/WindowChromeViewModel.h"
#include "ui/theme/Theme.h"
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

private:
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
    core::db::LibraryQueries m_library;
    core::UserDataOperationGate m_userDataGate;
    platform::windows::WindowsAudioPlayer m_audio;
    // Bound to the main window once it exists (showMainWindow).
    platform::windows::WindowsMediaControls m_mediaControls;
    core::LocalFileSourceResolver m_resolver;
    core::SystemPlaybackClock m_clock;
    core::PlayerController m_player;
    core::PlaybackStatePersistence m_persistence;
    core::MediaControlsBridge m_mediaBridge;

    ui::WindowChromeViewModel m_windowChromeViewModel;
    ui::Navigator m_navigator;
    ui::ShellLayoutViewModel m_layout;
    ui::SidebarViewModel m_sidebar;
    ui::ToastQueue m_toasts;
    ui::PlayerBarViewModel m_playerBar;
    ui::FullscreenPlayerViewModel m_fullscreen;
    ui::QueueRailViewModel m_queueRail;
    ui::AmbientAccentSampler m_ambient;
    ui::AppRoot m_appRoot;
    QPointer<ui::Theme> m_theme; // owned by the QML engine
};

} // namespace lmp::app
