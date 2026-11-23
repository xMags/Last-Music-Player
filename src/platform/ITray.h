#pragma once

#include <QObject>
#include <QString>

namespace lmp::platform {

// The app's icon in the notification area while its window is closed to
// the tray, with a menu: Show, Play/Pause, Next, Previous, Quit. A click on
// the icon shows the window.
//
// GUI thread only.
class ITray : public QObject {
    Q_OBJECT

public:
    ~ITray() override = default;
    ITray(const ITray&) = delete;
    ITray& operator=(const ITray&) = delete;

    // Adds the icon (once); false when the system refused it.
    virtual bool show(const QString& tooltip) = 0;
    virtual void hide() = 0;
    [[nodiscard]] virtual bool isShown() const = 0;

signals:
    void showRequested();
    void playPauseRequested();
    void nextRequested();
    void previousRequested();
    void quitRequested();

protected:
    explicit ITray(QObject* parent = nullptr) : QObject(parent) {}
};

} // namespace lmp::platform
