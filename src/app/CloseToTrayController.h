#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>

#include <functional>

namespace lmp::core {
class SettingsStore;
} // namespace lmp::core

namespace lmp::platform {
class ITray;
} // namespace lmp::platform

namespace lmp::app {

// What closing the main window does (the CloseBehavior setting, as the
// current app reads it): 0 hides the window and shows the tray icon, 1 quits.
// The tray icon brings the window back, plays or pauses, skips, and quits.
// Quitting goes through the application's normal end (the window placement
// is saved on aboutToQuit; playback, volume and downloads are saved when the
// composition root is destroyed).
//
// GUI thread only.
class CloseToTrayController final : public QObject {
    Q_OBJECT

public:
    struct Playback {
        std::function<void()> togglePlay;
        std::function<void()> next;
        std::function<void()> previous;
    };

    // `tray` and `settings` outlive this object.
    CloseToTrayController(platform::ITray& tray, core::SettingsStore& settings, Playback playback,
                          QObject* parent = nullptr);

    // Watches the window's close requests.
    void attach(QWindow* window);

    // True when closing the window keeps the app running in the tray.
    [[nodiscard]] bool closesToTray() const;
    // Shows the window again (from the tray, minimised, or behind others)
    // and takes the tray icon away.
    void restore();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void quit();

    platform::ITray& m_tray;
    core::SettingsStore& m_settings;
    Playback m_playback;
    QPointer<QWindow> m_window;
    bool m_quitting = false;
};

} // namespace lmp::app
