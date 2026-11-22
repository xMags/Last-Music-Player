#include "platform/windows/WindowsReducedMotion.h"

#include <QCoreApplication>

#include <utility>

#include <windows.h>

namespace lmp::platform::windows {

WindowsReducedMotion::WindowsReducedMotion()
    : m_reduced(readSystemSetting())
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsReducedMotion::~WindowsReducedMotion()
{
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
}

bool WindowsReducedMotion::reduced() const
{
    return m_reduced;
}

void WindowsReducedMotion::setChangeHandler(ChangeHandler handler)
{
    m_handler = std::move(handler);
}

bool WindowsReducedMotion::nativeEventFilter(const QByteArray& eventType, void* message,
                                             qintptr* /*result*/)
{
    if (eventType != "windows_generic_MSG" || message == nullptr) {
        return false;
    }
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_SETTINGCHANGE || msg->wParam != SPI_SETCLIENTAREAANIMATION) {
        return false;
    }
    // Every top-level window receives the broadcast; report each change once.
    const bool reduced = readSystemSetting();
    if (reduced == m_reduced) {
        return false;
    }
    m_reduced = reduced;
    if (m_handler) {
        const ChangeHandler handler = m_handler;
        handler(m_reduced);
    }
    // Never consume the message: Qt and the window need it too.
    return false;
}

bool WindowsReducedMotion::readSystemSetting()
{
    BOOL animationsOn = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animationsOn, 0)) {
        // Unknown: keep animations, which is the system default.
        return false;
    }
    return animationsOn == FALSE;
}

} // namespace lmp::platform::windows
