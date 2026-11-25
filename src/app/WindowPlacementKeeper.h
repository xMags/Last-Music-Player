#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWindow>

#include <optional>

namespace lmp::core {
class SettingsStore;
}

namespace lmp::platform {
class IWindowPlacement;
}

namespace lmp::app {

// Keeps the main window's frame between runs (core::WindowPlacement): puts
// the saved one back before the window first shows (startsMaximized says
// whether to show it maximised), and saves the frame, or that the window is
// maximised, each time a move, resize or maximise settles while the window
// shows, so a crash or a shutdown from the tray keeps it too. A maximised
// window keeps the frame it had before, which it restores to. The frame is
// not read at quit: by then the window is closing and its frame no longer
// reads as it showed.
//
// GUI thread only.
class WindowPlacementKeeper : public QObject {
    Q_OBJECT

public:
    // Both outlive this object.
    WindowPlacementKeeper(platform::IWindowPlacement& placement, core::SettingsStore& settings,
                          QObject* parent = nullptr);

    // The main window, before it is shown: the saved frame is applied.
    void attach(QWindow& window);
    // Whether the window was maximised when it last showed.
    [[nodiscard]] bool startsMaximized() const { return m_maximized; }
    // Writes the last frame noted (the application quits).
    void save();

private:
    void note();

    platform::IWindowPlacement& m_placement;
    core::SettingsStore& m_settings;
    QPointer<QWindow> m_window;
    QTimer m_settle;
    std::optional<QRect> m_last;
    bool m_maximized = false;
};

} // namespace lmp::app
