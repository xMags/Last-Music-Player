#pragma once

#include <QString>

#include "platform/ISingleInstance.h"

namespace lmp::platform::windows {

// One running copy, enforced with a named mutex; the running copy's window
// is found by its title. Both names are part of the installed identity. The
// second copy does not show that window itself: it posts the activation
// message, and the running copy restores its own window (Qt would otherwise
// still take a window shown from outside as hidden).
class WindowsSingleInstance final : public ISingleInstance {
public:
    WindowsSingleInstance(QString mutexName, QString windowTitle);
    ~WindowsSingleInstance() override;

    WindowsSingleInstance(const WindowsSingleInstance&) = delete;
    WindowsSingleInstance& operator=(const WindowsSingleInstance&) = delete;

    [[nodiscard]] bool tryAcquire() override;
    void activateExistingWindow() override;

    // The registered message a second copy posts to the running copy's
    // window (WindowsActivationRequests); 0 when it could not be registered.
    [[nodiscard]] static unsigned int activationMessage();

private:
    QString m_mutexName;
    QString m_windowTitle;
    // The mutex handle (a Win32 HANDLE), owned while this copy holds it.
    void* m_mutex = nullptr;
};

} // namespace lmp::platform::windows
