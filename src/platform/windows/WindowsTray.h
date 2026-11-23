#pragma once

#include "platform/ITray.h"

namespace lmp::platform::windows {

// The notification-area icon on the native API (Shell_NotifyIconW with a
// message-only window), as the earlier versions drew it, so the app needs no
// Qt Widgets module. The window procedure runs on the GUI thread (the thread
// that created the window), so the signals are emitted there directly.
class WindowsTray final : public ITray {
    Q_OBJECT

public:
    explicit WindowsTray(QObject* parent = nullptr);
    ~WindowsTray() override;

    bool show(const QString& tooltip) override;
    void hide() override;
    [[nodiscard]] bool isShown() const override { return m_added; }

    // Called by the window procedure.
    void handleNotification(unsigned int event);

private:
    void showMenu();

    // HWND, kept as an integer so this header needs no Windows header.
    quintptr m_window = 0;
    bool m_added = false;
};

} // namespace lmp::platform::windows
