#include "app/CloseToTrayController.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "platform/ITray.h"

#include <QCoreApplication>
#include <QEvent>

namespace lmp::app {

CloseToTrayController::CloseToTrayController(platform::ITray& tray, core::SettingsStore& settings, Playback playback,
                                             QObject* parent)
    : QObject(parent)
    , m_tray(tray)
    , m_settings(settings)
    , m_playback(std::move(playback))
{
    connect(&m_tray, &platform::ITray::showRequested, this, &CloseToTrayController::restore);
    connect(&m_tray, &platform::ITray::quitRequested, this, &CloseToTrayController::quit);
    connect(&m_tray, &platform::ITray::playPauseRequested, this, [this]() {
        if (m_playback.togglePlay) {
            m_playback.togglePlay();
        }
    });
    connect(&m_tray, &platform::ITray::nextRequested, this, [this]() {
        if (m_playback.next) {
            m_playback.next();
        }
    });
    connect(&m_tray, &platform::ITray::previousRequested, this, [this]() {
        if (m_playback.previous) {
            m_playback.previous();
        }
    });
}

void CloseToTrayController::attach(QWindow* window)
{
    if (window == nullptr) {
        return;
    }
    m_window = window;
    window->installEventFilter(this);
    // The application's own quit (QCoreApplication::quit, the session
    // ending) arrives as a Quit event before it closes the windows; those
    // closes must go through.
    QCoreApplication::instance()->installEventFilter(this);
}

bool CloseToTrayController::closesToTray() const
{
    // 0 (the default) minimises to the tray; anything else quits.
    return m_settings.integer(core::settings::keys::CloseBehavior, 0) == 0;
}

bool CloseToTrayController::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Quit) {
        m_quitting = true;
        return QObject::eventFilter(watched, event);
    }
    if (watched != m_window || event->type() != QEvent::Close || m_quitting || !closesToTray()) {
        return QObject::eventFilter(watched, event);
    }
    // Without a tray icon there would be no way back: then the window closes.
    if (!m_tray.show(QCoreApplication::applicationName())) {
        return QObject::eventFilter(watched, event);
    }
    event->ignore();
    m_window->hide();
    return true;
}

void CloseToTrayController::restore()
{
    if (m_window == nullptr) {
        return;
    }
    // A minimised window comes back as it was (maximised or not).
    if (m_window->windowStates() & Qt::WindowMinimized) {
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    }
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
    m_tray.hide();
}

void CloseToTrayController::quit()
{
    m_quitting = true;
    m_tray.hide();
    QCoreApplication::quit();
}

} // namespace lmp::app
