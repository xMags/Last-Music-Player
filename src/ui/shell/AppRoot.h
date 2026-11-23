#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "core/mode/Capabilities.h"
#include "ui/navigation/Navigator.h"
#include "ui/shell/ProfileViewModel.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/shell/SidebarViewModel.h"
#include "ui/player/FullscreenPlayerViewModel.h"
#include "ui/player/PlayerBarViewModel.h"
#include "ui/player/QueueRailViewModel.h"
#include "ui/home/HomeViewModel.h"
#include "ui/search/SearchViewModel.h"
#include "ui/library/CollectionEditor.h"
#include "ui/library/CollectionViewModel.h"
#include "ui/library/LibraryViewModel.h"
#include "ui/library/LocalArtistViewModel.h"
#include "ui/settings/DataSettingsViewModel.h"
#include "ui/settings/FirstRunViewModel.h"
#include "ui/tracks/TrackMenuController.h"
#include "ui/settings/LibrarySettingsViewModel.h"
#include "ui/settings/SettingsAboutViewModel.h"
#include "ui/settings/SettingsViewModel.h"
#include "ui/settings/SourceModeViewModel.h"
#include "ui/shell/WindowChromeViewModel.h"
#include "ui/toasts/ToastQueue.h"

class QJSEngine;
class QQmlEngine;

namespace lmp::core::mode {
class ModeService;
}

namespace lmp::ui {

// What the user can do now, for QML: `App.capabilities.searchOnline` and so on.
struct CapabilitiesForeign {
    Q_GADGET
    QML_FOREIGN(lmp::core::mode::Capabilities)
    QML_VALUE_TYPE(capabilities)
};

// The `App` singleton: the shell-level view models for QML. The composition
// root creates the one instance and keeps ownership; QML receives it through
// create(). The pointer kept for create() is the only static state allowed in
// the project.
class AppRoot : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::WindowChromeViewModel* windowChrome READ windowChrome CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::Navigator* navigator READ navigator CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::ShellLayoutViewModel* layout READ layout CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::SidebarViewModel* sidebar READ sidebar CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::PlayerBarViewModel* playerBar READ playerBar CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::FullscreenPlayerViewModel* fullscreen READ fullscreen CONSTANT FINAL)
    // The Up Next list, in the queue rail and the full-screen player.
    Q_PROPERTY(lmp::ui::QueueRailViewModel* queueRail READ queueRail CONSTANT FINAL)
    // The short messages above the player bar.
    Q_PROPERTY(lmp::ui::ToastQueue* toasts READ toasts CONSTANT FINAL)
    // The preference rows and the About card of Settings.
    Q_PROPERTY(lmp::ui::SettingsViewModel* settings READ settings CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::SettingsAboutViewModel* settingsAbout READ settingsAbout CONSTANT FINAL)
    // The music source cards, API key form, account block and sign-in.
    Q_PROPERTY(lmp::ui::SourceModeViewModel* source READ source CONSTANT FINAL)
    // The one-time music source choice shown before the shell.
    Q_PROPERTY(lmp::ui::FirstRunViewModel* firstRun READ firstRun CONSTANT FINAL)
    // Who the app presents: the sidebar's profile card and its menu.
    Q_PROPERTY(lmp::ui::ProfileViewModel* profile READ profile CONSTANT FINAL)
    // The Library card of Settings: music folder, scan and formats.
    Q_PROPERTY(lmp::ui::LibrarySettingsViewModel* librarySettings READ librarySettings CONSTANT FINAL)
    // The Library page.
    Q_PROPERTY(lmp::ui::LibraryViewModel* library READ library CONSTANT FINAL)
    // The song menu every page shares.
    Q_PROPERTY(lmp::ui::TrackMenuController* trackMenu READ trackMenu CONSTANT FINAL)
    // The album, genre and playlist pages, and the local artist page.
    Q_PROPERTY(lmp::ui::CollectionViewModel* collection READ collection CONSTANT FINAL)
    Q_PROPERTY(lmp::ui::LocalArtistViewModel* localArtist READ localArtist CONSTANT FINAL)
    // New, rename and delete for playlists and albums, with their dialogs.
    Q_PROPERTY(lmp::ui::CollectionEditor* collections READ collections CONSTANT FINAL)
    // Home in its offline form.
    Q_PROPERTY(lmp::ui::HomeViewModel* home READ home CONSTANT FINAL)
    // Browse and search.
    Q_PROPERTY(lmp::ui::SearchViewModel* search READ search CONSTANT FINAL)
    // The Settings Data card.
    Q_PROPERTY(lmp::ui::DataSettingsViewModel* dataSettings READ dataSettings CONSTANT FINAL)
    // The current music source: "LocalOnly", "ApiKey" or "Account".
    Q_PROPERTY(QString sourceMode READ sourceMode NOTIFY sourceChanged FINAL)
    // What the current source lets the user do (core::mode::Capabilities).
    Q_PROPERTY(lmp::core::mode::Capabilities capabilities READ capabilities NOTIFY sourceChanged FINAL)
    // Started with --gallery: the design gallery shows instead of the shell.
    Q_PROPERTY(bool showGallery READ showGallery CONSTANT FINAL)

public:
    // The view models are owned by the composition root and outlive this.
    struct Parts {
        WindowChromeViewModel* windowChrome = nullptr;
        Navigator* navigator = nullptr;
        ShellLayoutViewModel* layout = nullptr;
        SidebarViewModel* sidebar = nullptr;
        PlayerBarViewModel* playerBar = nullptr;
        FullscreenPlayerViewModel* fullscreen = nullptr;
        QueueRailViewModel* queueRail = nullptr;
        ToastQueue* toasts = nullptr;
        SettingsViewModel* settings = nullptr;
        SettingsAboutViewModel* settingsAbout = nullptr;
        SourceModeViewModel* source = nullptr;
        FirstRunViewModel* firstRun = nullptr;
        ProfileViewModel* profile = nullptr;
        LibrarySettingsViewModel* librarySettings = nullptr;
        LibraryViewModel* library = nullptr;
        TrackMenuController* trackMenu = nullptr;
        CollectionViewModel* collection = nullptr;
        LocalArtistViewModel* localArtist = nullptr;
        CollectionEditor* collections = nullptr;
        HomeViewModel* home = nullptr;
        SearchViewModel* search = nullptr;
        DataSettingsViewModel* dataSettings = nullptr;
        core::mode::ModeService* mode = nullptr;
        bool showGallery = false;
    };

    explicit AppRoot(Parts parts, QObject* parent = nullptr);
    ~AppRoot() override;

    AppRoot(const AppRoot&) = delete;
    AppRoot& operator=(const AppRoot&) = delete;

    [[nodiscard]] QString version() const;
    [[nodiscard]] WindowChromeViewModel* windowChrome() const { return m_parts.windowChrome; }
    [[nodiscard]] Navigator* navigator() const { return m_parts.navigator; }
    [[nodiscard]] ShellLayoutViewModel* layout() const { return m_parts.layout; }
    [[nodiscard]] SidebarViewModel* sidebar() const { return m_parts.sidebar; }
    [[nodiscard]] PlayerBarViewModel* playerBar() const { return m_parts.playerBar; }
    [[nodiscard]] FullscreenPlayerViewModel* fullscreen() const { return m_parts.fullscreen; }
    [[nodiscard]] QueueRailViewModel* queueRail() const { return m_parts.queueRail; }
    [[nodiscard]] ToastQueue* toasts() const { return m_parts.toasts; }
    [[nodiscard]] SettingsViewModel* settings() const { return m_parts.settings; }
    [[nodiscard]] SettingsAboutViewModel* settingsAbout() const { return m_parts.settingsAbout; }
    [[nodiscard]] SourceModeViewModel* source() const { return m_parts.source; }
    [[nodiscard]] FirstRunViewModel* firstRun() const { return m_parts.firstRun; }
    [[nodiscard]] ProfileViewModel* profile() const { return m_parts.profile; }
    [[nodiscard]] LibrarySettingsViewModel* librarySettings() const { return m_parts.librarySettings; }
    [[nodiscard]] LibraryViewModel* library() const { return m_parts.library; }
    [[nodiscard]] TrackMenuController* trackMenu() const { return m_parts.trackMenu; }
    [[nodiscard]] CollectionViewModel* collection() const { return m_parts.collection; }
    [[nodiscard]] LocalArtistViewModel* localArtist() const { return m_parts.localArtist; }
    [[nodiscard]] CollectionEditor* collections() const { return m_parts.collections; }
    [[nodiscard]] HomeViewModel* home() const { return m_parts.home; }
    [[nodiscard]] SearchViewModel* search() const { return m_parts.search; }
    [[nodiscard]] DataSettingsViewModel* dataSettings() const { return m_parts.dataSettings; }
    [[nodiscard]] QString sourceMode() const;
    [[nodiscard]] core::mode::Capabilities capabilities() const;
    [[nodiscard]] bool showGallery() const { return m_parts.showGallery; }

    // Called by the QML engine the first time QML uses `App`. Returns the
    // instance the composition root registered, with C++ ownership.
    static AppRoot* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // The composition root registers the instance before loading any QML and
    // clears it before destroying it.
    static void registerInstance(AppRoot* instance);

signals:
    void sourceChanged();

private:
    Parts m_parts;
};

} // namespace lmp::ui
