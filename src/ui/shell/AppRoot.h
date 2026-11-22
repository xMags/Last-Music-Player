#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/navigation/Navigator.h"
#include "ui/shell/ShellLayoutViewModel.h"
#include "ui/shell/SidebarViewModel.h"
#include "ui/player/FullscreenPlayerViewModel.h"
#include "ui/player/PlayerBarViewModel.h"
#include "ui/player/QueueRailViewModel.h"
#include "ui/shell/WindowChromeViewModel.h"
#include "ui/toasts/ToastQueue.h"

class QJSEngine;
class QQmlEngine;

namespace lmp::ui {

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
    [[nodiscard]] bool showGallery() const { return m_parts.showGallery; }

    // Called by the QML engine the first time QML uses `App`. Returns the
    // instance the composition root registered, with C++ ownership.
    static AppRoot* create(QQmlEngine* qmlEngine, QJSEngine* jsEngine);

    // The composition root registers the instance before loading any QML and
    // clears it before destroying it.
    static void registerInstance(AppRoot* instance);

private:
    Parts m_parts;
};

} // namespace lmp::ui
