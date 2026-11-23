#include "platform/windows/WindowsTray.h"

#include <windows.h>
#include <shellapi.h>

#include <iterator>
#include <string>

namespace lmp::platform::windows {

namespace {

constexpr UINT TrayCallbackMessage = WM_APP + 1;
constexpr UINT TrayIconId = 1;
constexpr UINT MenuShow = 101;
constexpr UINT MenuPlayPause = 102;
constexpr UINT MenuNext = 103;
constexpr UINT MenuPrevious = 104;
constexpr UINT MenuQuit = 105;
constexpr wchar_t WindowClassName[] = L"LastMusicPlayerTrayWindow";

[[nodiscard]] HWND windowOf(quintptr handle)
{
    // The handle was stored as an integer; this is the low-level API's own type.
    return reinterpret_cast<HWND>(handle);
}

LRESULT CALLBACK trayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == TrayCallbackMessage) {
        // The owner was stored on the window when it was made; it removes the
        // window before it goes away.
        auto* tray = reinterpret_cast<WindowsTray*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (tray != nullptr) {
            tray->handleNotification(LOWORD(lParam));
        }
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

WindowsTray::WindowsTray(QObject* parent)
    : ITray(parent)
{
}

WindowsTray::~WindowsTray()
{
    hide();
    if (m_window != 0) {
        SetWindowLongPtrW(windowOf(m_window), GWLP_USERDATA, 0);
        DestroyWindow(windowOf(m_window));
        m_window = 0;
    }
}

bool WindowsTray::show(const QString& tooltip)
{
    if (m_added) {
        return true;
    }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (m_window == 0) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &trayWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = WindowClassName;
        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        const HWND window =
            CreateWindowExW(0, WindowClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        if (window == nullptr) {
            return false;
        }
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_window = reinterpret_cast<quintptr>(window);
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = windowOf(m_window);
    data.uID = TrayIconId;
    data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    data.uCallbackMessage = TrayCallbackMessage;
    // The app's own icon from its resources (AppIcon.rc), at the small size.
    data.hIcon = static_cast<HICON>(LoadImageW(instance, L"IDI_ICON1", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
    const std::wstring tip = tooltip.left(static_cast<qsizetype>(std::size(data.szTip)) - 1).toStdWString();
    wcsncpy_s(data.szTip, tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &data)) {
        return false;
    }
    m_added = true;
    return true;
}

void WindowsTray::hide()
{
    if (!m_added) {
        return;
    }
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = windowOf(m_window);
    data.uID = TrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
    m_added = false;
}

void WindowsTray::handleNotification(unsigned int event)
{
    switch (event) {
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        emit showRequested();
        break;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        showMenu();
        break;
    default:
        break;
    }
}

void WindowsTray::showMenu()
{
    POINT point{};
    GetCursorPos(&point);
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }
    AppendMenuW(menu, MF_STRING, MenuShow, tr("Show").toStdWString().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuPlayPause, tr("Play/Pause").toStdWString().c_str());
    AppendMenuW(menu, MF_STRING, MenuNext, tr("Next").toStdWString().c_str());
    AppendMenuW(menu, MF_STRING, MenuPrevious, tr("Previous").toStdWString().c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuQuit, tr("Quit").toStdWString().c_str());
    // Needed so the menu closes when the user clicks elsewhere.
    SetForegroundWindow(windowOf(m_window));
    const UINT command = static_cast<UINT>(
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0, windowOf(m_window), nullptr));
    DestroyMenu(menu);
    switch (command) {
    case MenuShow:
        emit showRequested();
        break;
    case MenuPlayPause:
        emit playPauseRequested();
        break;
    case MenuNext:
        emit nextRequested();
        break;
    case MenuPrevious:
        emit previousRequested();
        break;
    case MenuQuit:
        emit quitRequested();
        break;
    default:
        break;
    }
}

} // namespace lmp::platform::windows
