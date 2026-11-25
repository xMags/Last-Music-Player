#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <optional>

namespace lmp::platform {

// The main window's frame on screen, in physical pixels with the outer frame
// included (the units the current app saved).
//
// GUI thread only.
class IWindowPlacement {
public:
    virtual ~IWindowPlacement() = default;

    // The frame now; nothing while the window is minimised.
    [[nodiscard]] virtual std::optional<QRect> frame(quintptr window) const = 0;
    // Whether the window is maximised now (false while it is minimised).
    [[nodiscard]] virtual bool maximized(quintptr window) const = 0;
    // Sets the frame's size and, when `position` is given and a monitor still
    // shows that place, its corner (otherwise the system places it).
    virtual void apply(quintptr window, const QSize& size, const std::optional<QPoint>& position) = 0;

protected:
    IWindowPlacement() = default;
    IWindowPlacement(const IWindowPlacement&) = default;
    IWindowPlacement& operator=(const IWindowPlacement&) = default;
};

} // namespace lmp::platform
