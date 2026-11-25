#include "platform/windows/WindowsActivationRequests.h"

#include "platform/windows/WindowsSingleInstance.h"

#include <windows.h>

namespace lmp::platform::windows {

WindowsActivationRequests::WindowsActivationRequests(QObject* parent)
    : QObject(parent)
    , m_message(WindowsSingleInstance::activationMessage())
{
}

bool WindowsActivationRequests::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    if (m_message == 0 || eventType != "windows_generic_MSG" || message == nullptr) {
        return false;
    }
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->message != m_message) {
        return false;
    }
    emit activationRequested();
    if (result != nullptr) {
        *result = 0;
    }
    return true;
}

} // namespace lmp::platform::windows
