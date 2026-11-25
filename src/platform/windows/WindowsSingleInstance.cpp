#include "platform/windows/WindowsSingleInstance.h"

#include <QLoggingCategory>

#include <string>
#include <utility>

#include <windows.h>

namespace lmp::platform::windows {

namespace {

Q_LOGGING_CATEGORY(lcSingleInstance, "lmp.platform.singleinstance")

// The property the running copy sets on its main window.
constexpr wchar_t MainWindowProperty[] = L"LastMusicPlayer.MainWindow";

BOOL CALLBACK findMarkedWindow(HWND window, LPARAM found)
{
    if (GetPropW(window, MainWindowProperty) == nullptr) {
        return TRUE;
    }
    *reinterpret_cast<HWND*>(found) = window;
    return FALSE;
}

} // namespace

void WindowsSingleInstance::markMainWindow(quintptr window)
{
    // Any non-null value marks it; the property goes with the window.
    if (window != 0) {
        SetPropW(reinterpret_cast<HWND>(window), MainWindowProperty, reinterpret_cast<HANDLE>(1));
    }
}

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

unsigned int WindowsSingleInstance::activationMessage()
{
    static const UINT message = RegisterWindowMessageW(L"LastMusicPlayer.Activate");
    return message;
}

void WindowsSingleInstance::activateExistingWindow()
{
    // The marked window; a copy that does not mark it (an earlier version) is
    // found by its title.
    HWND window = nullptr;
    EnumWindows(&findMarkedWindow, reinterpret_cast<LPARAM>(&window));
    if (window == nullptr) {
        const std::wstring title = m_windowTitle.toStdWString();
        window = FindWindowW(nullptr, title.c_str());
    }
    if (window == nullptr) {
        return;
    }
    // The running copy may take the foreground: this process has it now
    // (the user just started it), and may hand it on.
    DWORD processId = 0;
    if (GetWindowThreadProcessId(window, &processId) != 0 && processId != 0) {
        AllowSetForegroundWindow(processId);
    }
    const UINT message = activationMessage();
    if (message == 0 || !PostMessageW(window, message, 0, 0)) {
        qCWarning(lcSingleInstance) << "Could not ask the running copy to show itself, error" << GetLastError();
    }
}

} // namespace lmp::platform::windows
