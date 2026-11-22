#pragma once

#include <QString>

#include "platform/ISingleInstance.h"

namespace lmp::platform::windows {

// One running copy, enforced with a named mutex; the running copy's window
// is found by its title. Both names are part of the installed identity.
class WindowsSingleInstance final : public ISingleInstance {
public:
    WindowsSingleInstance(QString mutexName, QString windowTitle);
    ~WindowsSingleInstance() override;

    WindowsSingleInstance(const WindowsSingleInstance&) = delete;
    WindowsSingleInstance& operator=(const WindowsSingleInstance&) = delete;

    [[nodiscard]] bool tryAcquire() override;
    void activateExistingWindow() override;

private:
    QString m_mutexName;
    QString m_windowTitle;
    // The mutex handle (a Win32 HANDLE), owned while this copy holds it.
    void* m_mutex = nullptr;
};

} // namespace lmp::platform::windows
