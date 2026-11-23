#include "platform/windows/WindowsWindowPlacement.h"

#include <windows.h>

namespace lmp::platform::windows {

namespace {

HWND handleOf(quintptr window)
{
    return reinterpret_cast<HWND>(window); // a WId is the HWND on Windows
}

} // namespace

std::optional<QRect> WindowsWindowPlacement::frame(quintptr window) const
{
    const HWND hwnd = handleOf(window);
    if (hwnd == nullptr || IsIconic(hwnd)) {
        return std::nullopt;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return std::nullopt;
    }
    return QRect(QPoint(rect.left, rect.top), QSize(rect.right - rect.left, rect.bottom - rect.top));
}

void WindowsWindowPlacement::apply(quintptr window, const QSize& size, const std::optional<QPoint>& position)
{
    const HWND hwnd = handleOf(window);
    if (hwnd == nullptr || !size.isValid()) {
        return;
    }
    UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    int x = 0;
    int y = 0;
    if (position) {
        // The monitor the window was on may be gone: then the system places it.
        const RECT probe{position->x(), position->y(), position->x() + size.width(), position->y() + size.height()};
        if (MonitorFromRect(&probe, MONITOR_DEFAULTTONULL) != nullptr) {
            x = position->x();
            y = position->y();
        } else {
            flags |= SWP_NOMOVE;
        }
    } else {
        flags |= SWP_NOMOVE;
    }
    SetWindowPos(hwnd, nullptr, x, y, size.width(), size.height(), flags);
}

} // namespace lmp::platform::windows
