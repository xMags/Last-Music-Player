#pragma once

#include "platform/IWindowPlacement.h"

namespace lmp::platform::windows {

// IWindowPlacement with GetWindowRect, IsIconic, MonitorFromRect and
// SetWindowPos, as the current app restored and saved its window.
class WindowsWindowPlacement final : public IWindowPlacement {
public:
    [[nodiscard]] std::optional<QRect> frame(quintptr window) const override;
    void apply(quintptr window, const QSize& size, const std::optional<QPoint>& position) override;
};

} // namespace lmp::platform::windows
