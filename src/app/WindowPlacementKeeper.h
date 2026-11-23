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
// the saved one back before the window first shows, notes the frame each
// time a move or resize settles while the window shows, and saves the last
// one noted when the application quits. The frame is not read at quit: by
// then the window is closing and its frame no longer reads as it showed.
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
    // Writes the last frame noted (the application quits).
    void save();

private:
    void note();

    platform::IWindowPlacement& m_placement;
    core::SettingsStore& m_settings;
    QPointer<QWindow> m_window;
    QTimer m_settle;
    std::optional<QRect> m_last;
};

} // namespace lmp::app
