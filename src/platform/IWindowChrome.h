#pragma once

#include <QRectF>
#include <QtGlobal>

#include <functional>

namespace lmp::platform {

// The frame of the main window when the application draws its own title
// strip: the system caption is removed while resizing, dragging, snapping and
// maximising keep their native behaviour. Rectangles and heights are in the
// window's logical (device-independent) pixels, measured from its top-left.
class IWindowChrome {
public:
    using StateHandler = std::function<void()>;

    virtual ~IWindowChrome() = default;

    // Takes over the frame of the native window `windowHandle`. Returns false
    // when it cannot, in which case the system frame stays.
    [[nodiscard]] virtual bool attach(quintptr windowHandle) = 0;

    // A dark system frame, for the dark theme.
    virtual void setDarkFrame(bool dark) = 0;
    // Asks for the system backdrop material behind the window. Returns whether
    // it is now active; the window must then draw its frame see-through.
    virtual bool setBackdrop(bool enabled) = 0;
    [[nodiscard]] virtual bool backdropActive() const = 0;

    // The strip along the top edge that drags the window, and the three
    // caption buttons the interface draws inside it.
    virtual void setDragStripHeight(qreal height) = 0;
    virtual void setCaptionButtons(const QRectF& minimize, const QRectF& maximize,
                                   const QRectF& close) = 0;

    virtual void minimize() = 0;
    virtual void toggleMaximize() = 0;
    virtual void close() = 0;

    [[nodiscard]] virtual bool maximized() const = 0;
    [[nodiscard]] virtual bool active() const = 0;
    // The maximise button is answered by the system (so it can offer snap
    // layouts); these say whether the pointer is over it and pressing it.
    [[nodiscard]] virtual bool maximizeHovered() const = 0;
    [[nodiscard]] virtual bool maximizePressed() const = 0;
    // True while the user drags the window to another place, from the
    // first step of the move to its end. Not while resizing it.
    [[nodiscard]] virtual bool moving() const = 0;

    // Called on the window's thread after any of the states above changes.
    virtual void setStateHandler(StateHandler handler) = 0;

protected:
    IWindowChrome() = default;
    IWindowChrome(const IWindowChrome&) = default;
    IWindowChrome& operator=(const IWindowChrome&) = default;
};

} // namespace lmp::platform
