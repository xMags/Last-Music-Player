#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QRectF>

#include "platform/IWindowChrome.h"

namespace lmp::platform::windows {

// Removes the caption of one top-level window while keeping its resize frame,
// drop shadow, snapping and maximise animation. The interface draws its own
// title strip; this class answers the hit tests so the strip drags the window
// and the maximise button behaves as the system's own (snap layouts included).
// Watches the window's messages through a native event filter it installs on
// the application, on the GUI thread; all callbacks run there.
class WindowsWindowChrome final : public IWindowChrome, public QAbstractNativeEventFilter {
public:
    WindowsWindowChrome();
    ~WindowsWindowChrome() override;

    WindowsWindowChrome(const WindowsWindowChrome&) = delete;
    WindowsWindowChrome& operator=(const WindowsWindowChrome&) = delete;

    [[nodiscard]] bool attach(quintptr windowHandle) override;
    void setDarkFrame(bool dark) override;
    bool setBackdrop(bool enabled) override;
    [[nodiscard]] bool backdropActive() const override { return m_backdrop; }
    void setDragStripHeight(qreal height) override;
    void setCaptionButtons(const QRectF& minimize, const QRectF& maximize,
                           const QRectF& close) override;
    void minimize() override;
    void toggleMaximize() override;
    void close() override;
    [[nodiscard]] bool maximized() const override;
    [[nodiscard]] bool active() const override { return m_active; }
    [[nodiscard]] bool maximizeHovered() const override { return m_maximizeHovered; }
    [[nodiscard]] bool maximizePressed() const override { return m_maximizePressed; }
    void setStateHandler(StateHandler handler) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

private:
    // Each handler returns true when it produced the message's result.
    bool onCalcSize(quintptr wparam, qintptr lparam, qintptr* result);
    bool onHitTest(qintptr lparam, qintptr* result);
    bool onNonClientButton(unsigned int message, quintptr hit, qintptr* result);
    void onNonClientMove(quintptr hit);
    void setMaximizeState(bool hovered, bool pressed);
    void notify();

    [[nodiscard]] qreal scale() const;
    [[nodiscard]] int resizeBorderX() const;
    [[nodiscard]] int resizeBorderY() const;

    void* m_window = nullptr; // HWND of the attached window
    qreal m_dragStripHeight = 0;
    QRectF m_minimizeButton;
    QRectF m_maximizeButton;
    QRectF m_closeButton;
    bool m_active = true;
    bool m_backdrop = false;
    bool m_maximizeHovered = false;
    bool m_maximizePressed = false;
    StateHandler m_handler;
};

} // namespace lmp::platform::windows
