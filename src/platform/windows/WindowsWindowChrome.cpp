#include "platform/windows/WindowsWindowChrome.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QPointF>

#include <algorithm>
#include <utility>

#include <windows.h>
#include <dwmapi.h>

namespace lmp::platform::windows {

namespace {

Q_LOGGING_CATEGORY(lcWindowChrome, "lmp.platform.windowchrome")

// One pixel of frame inside the client keeps the drop shadow and the thin top
// frame line. The backdrop needs the frame under the whole client; it grows up
// from the bottom edge, not as a "sheet of glass", because the glass also
// extends the top frame where DWM draws its own caption buttons.
constexpr MARGINS kFrameMargins{0, 0, 1, 0};
constexpr MARGINS kBackdropMargins{0, 0, 0, 32000};

// Window attributes spelled out so the build does not depend on the SDK version.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeBefore20H1 = 19;
constexpr DWORD kSystemBackdropType = 38;
constexpr int kBackdropNone = 1;
constexpr int kBackdropMica = 2;

// The top edge resizes the window over a grip at least this tall (in DIP).
constexpr int kTopResizeGripDip = 6;

HWND asWindow(void* handle)
{
    return static_cast<HWND>(handle);
}

int signedLow(qintptr value)
{
    return static_cast<short>(LOWORD(static_cast<DWORD_PTR>(value)));
}

int signedHigh(qintptr value)
{
    return static_cast<short>(HIWORD(static_cast<DWORD_PTR>(value)));
}

} // namespace

WindowsWindowChrome::WindowsWindowChrome()
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WindowsWindowChrome::~WindowsWindowChrome()
{
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->removeNativeEventFilter(this);
    }
}

bool WindowsWindowChrome::attach(quintptr windowHandle)
{
    HWND window = reinterpret_cast<HWND>(windowHandle); // a WId is the HWND on Windows
    if (window == nullptr || !IsWindow(window)) {
        return false;
    }
    m_window = window;
    if (FAILED(DwmExtendFrameIntoClientArea(window, &kFrameMargins))) {
        qCWarning(lcWindowChrome) << "Could not extend the frame into the client area";
    }
    // Recalculates the frame now, so the caption disappears at once.
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    m_active = GetForegroundWindow() == window;
    notify();
    return true;
}

void WindowsWindowChrome::setDarkFrame(bool dark)
{
    if (m_window == nullptr) {
        return;
    }
    BOOL value = dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(asWindow(m_window), kUseImmersiveDarkMode, &value,
                                     sizeof(value)))) {
        // Windows 10 before 20H1 used the older attribute number.
        if (FAILED(DwmSetWindowAttribute(asWindow(m_window), kUseImmersiveDarkModeBefore20H1,
                                         &value, sizeof(value)))) {
            qCInfo(lcWindowChrome) << "This version of Windows has no dark window frame";
        }
    }
}

bool WindowsWindowChrome::setBackdrop(bool enabled)
{
    if (m_window == nullptr || enabled == m_backdrop) {
        return m_backdrop;
    }
    HWND window = asWindow(m_window);
    const int type = enabled ? kBackdropMica : kBackdropNone;
    const HRESULT applied = DwmSetWindowAttribute(window, kSystemBackdropType, &type, sizeof(type));
    if (enabled && FAILED(applied)) {
        qCInfo(lcWindowChrome) << "No system backdrop on this version of Windows";
        return false;
    }
    if (FAILED(DwmExtendFrameIntoClientArea(window, enabled ? &kBackdropMargins : &kFrameMargins))
        && enabled) {
        qCWarning(lcWindowChrome) << "Could not extend the frame for the backdrop";
        const int none = kBackdropNone;
        DwmSetWindowAttribute(window, kSystemBackdropType, &none, sizeof(none));
        return false;
    }
    m_backdrop = enabled;
    notify();
    return m_backdrop;
}

void WindowsWindowChrome::setDragStripHeight(qreal height)
{
    m_dragStripHeight = std::max<qreal>(0, height);
}

void WindowsWindowChrome::setCaptionButtons(const QRectF& minimize, const QRectF& maximize,
                                            const QRectF& close)
{
    m_minimizeButton = minimize;
    m_maximizeButton = maximize;
    m_closeButton = close;
}

void WindowsWindowChrome::minimize()
{
    if (m_window != nullptr) {
        ShowWindow(asWindow(m_window), SW_MINIMIZE);
    }
}

void WindowsWindowChrome::toggleMaximize()
{
    if (m_window != nullptr) {
        ShowWindow(asWindow(m_window), maximized() ? SW_RESTORE : SW_MAXIMIZE);
    }
}

void WindowsWindowChrome::close()
{
    if (m_window != nullptr) {
        PostMessageW(asWindow(m_window), WM_CLOSE, 0, 0);
    }
}

bool WindowsWindowChrome::maximized() const
{
    return m_window != nullptr && IsZoomed(asWindow(m_window)) != FALSE;
}

void WindowsWindowChrome::setStateHandler(StateHandler handler)
{
    m_handler = std::move(handler);
}

bool WindowsWindowChrome::nativeEventFilter(const QByteArray& eventType, void* message,
                                            qintptr* result)
{
    // Qt passes no result for posted messages (mouse input arrives that way);
    // the handlers then write into a local that nobody reads.
    qintptr unused = 0;
    if (result == nullptr) {
        result = &unused;
    }
    if (m_window == nullptr || message == nullptr || eventType != "windows_generic_MSG") {
        return false;
    }
    const MSG* msg = static_cast<const MSG*>(message);
    if (msg->hwnd != asWindow(m_window)) {
        return false;
    }
    switch (msg->message) {
    case WM_NCCALCSIZE:
        return onCalcSize(msg->wParam, msg->lParam, result);
    case WM_NCHITTEST:
        return onHitTest(msg->lParam, result);
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
        return onNonClientButton(msg->message, msg->wParam, result);
    case WM_NCMOUSEMOVE:
        onNonClientMove(msg->wParam);
        return false;
    case WM_NCMOUSELEAVE:
    case WM_MOUSEMOVE:
        setMaximizeState(false, false);
        return false;
    case WM_ACTIVATE:
        m_active = LOWORD(msg->wParam) != WA_INACTIVE;
        notify();
        return false;
    case WM_SIZE:
        notify();
        return false;
    default:
        return false;
    }
}

bool WindowsWindowChrome::onCalcSize(quintptr wparam, qintptr lparam, qintptr* result)
{
    if (wparam == FALSE) {
        return false;
    }
    // The structure layout is fixed by the Win32 API; lParam carries its address.
    auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
    const RECT proposed = params->rgrc[0];
    // Left, right and bottom keep the system's (invisible) resize frame, which
    // lies outside the visible edge; only the caption goes. A maximised window
    // hangs its frame past the monitor edges, so its top keeps the border too.
    DefWindowProcW(asWindow(m_window), WM_NCCALCSIZE, wparam, lparam);
    RECT client = params->rgrc[0];
    client.top = maximized() ? std::min(proposed.bottom, proposed.top + resizeBorderY())
                             : proposed.top;
    client.bottom = std::max(client.top, client.bottom);
    params->rgrc[0] = client;
    *result = 0;
    return true;
}

bool WindowsWindowChrome::onHitTest(qintptr lparam, qintptr* result)
{
    HWND window = asWindow(m_window);
    POINT point{signedLow(lparam), signedHigh(lparam)};
    RECT client{};
    GetClientRect(window, &client);
    POINT local = point;
    ScreenToClient(window, &local);
    // Outside the client lies the system's resize frame; let it answer.
    if (local.x < client.left || local.x >= client.right || local.y >= client.bottom) {
        return false;
    }
    const int topGrip = std::max(resizeBorderY(), qRound(kTopResizeGripDip * scale()));
    if (!maximized() && local.y < topGrip) {
        const int corner = resizeBorderX() * 2;
        *result = local.x < corner ? HTTOPLEFT : local.x >= client.right - corner ? HTTOPRIGHT : HTTOP;
        return true;
    }
    const QPointF logical(local.x / scale(), local.y / scale());
    if (m_maximizeButton.contains(logical)) {
        *result = HTMAXBUTTON;
        return true;
    }
    if (m_minimizeButton.contains(logical) || m_closeButton.contains(logical)) {
        *result = HTCLIENT;
        return true;
    }
    *result = logical.y() < m_dragStripHeight ? HTCAPTION : HTCLIENT;
    return true;
}

bool WindowsWindowChrome::onNonClientButton(unsigned int message, quintptr hit, qintptr* result)
{
    if (hit != HTMAXBUTTON) {
        return false;
    }
    // The system would draw and track its own button here; the interface
    // draws it, so the press and release are handled instead.
    if (message == WM_NCLBUTTONUP && m_maximizePressed) {
        setMaximizeState(true, false);
        toggleMaximize();
    } else if (message != WM_NCLBUTTONUP) {
        setMaximizeState(true, true);
    }
    *result = 0;
    return true;
}

void WindowsWindowChrome::onNonClientMove(quintptr hit)
{
    const bool overMaximize = hit == HTMAXBUTTON;
    if (overMaximize) {
        TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT,
                              asWindow(m_window), 0};
        TrackMouseEvent(&track);
    }
    setMaximizeState(overMaximize, overMaximize && m_maximizePressed);
}

void WindowsWindowChrome::setMaximizeState(bool hovered, bool pressed)
{
    if (hovered == m_maximizeHovered && pressed == m_maximizePressed) {
        return;
    }
    m_maximizeHovered = hovered;
    m_maximizePressed = pressed;
    notify();
}

void WindowsWindowChrome::notify()
{
    if (m_handler) {
        const StateHandler handler = m_handler;
        handler();
    }
}

qreal WindowsWindowChrome::scale() const
{
    const UINT dpi = m_window != nullptr ? GetDpiForWindow(asWindow(m_window)) : USER_DEFAULT_SCREEN_DPI;
    return static_cast<qreal>(dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi) / USER_DEFAULT_SCREEN_DPI;
}

int WindowsWindowChrome::resizeBorderX() const
{
    const UINT dpi = GetDpiForWindow(asWindow(m_window));
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

int WindowsWindowChrome::resizeBorderY() const
{
    const UINT dpi = GetDpiForWindow(asWindow(m_window));
    return GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

} // namespace lmp::platform::windows
