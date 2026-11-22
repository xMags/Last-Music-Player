#include "platform/windows/WindowsSingleInstance.h"

#include <QLoggingCategory>

#include <string>
#include <utility>

#include <windows.h>

namespace lmp::platform::windows {

namespace {

Q_LOGGING_CATEGORY(lcSingleInstance, "lmp.platform.singleinstance")

} // namespace

WindowsSingleInstance::WindowsSingleInstance(QString mutexName, QString windowTitle)
    : m_mutexName(std::move(mutexName))
    , m_windowTitle(std::move(windowTitle))
{
}

WindowsSingleInstance::~WindowsSingleInstance()
{
    if (m_mutex != nullptr) {
        ReleaseMutex(static_cast<HANDLE>(m_mutex));
        CloseHandle(static_cast<HANDLE>(m_mutex));
    }
}

bool WindowsSingleInstance::tryAcquire()
{
    if (m_mutex != nullptr) {
        return true;
    }
    const std::wstring name = m_mutexName.toStdWString();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (mutex == nullptr) {
        // Without the mutex there is no way to tell; run rather than refuse.
        qCWarning(lcSingleInstance) << "Could not create the single-instance mutex, error"
                                    << GetLastError();
        return true;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return false;
    }
    m_mutex = mutex;
    return true;
}

void WindowsSingleInstance::activateExistingWindow()
{
    const std::wstring title = m_windowTitle.toStdWString();
    HWND window = FindWindowW(nullptr, title.c_str());
    if (window == nullptr) {
        return;
    }
    ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
    if (!SetForegroundWindow(window)) {
        qCInfo(lcSingleInstance) << "Windows did not let the running copy come to the front";
    }
}

} // namespace lmp::platform::windows
