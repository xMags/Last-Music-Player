#pragma once

#include "platform/ITray.h"

#include <QString>

namespace lmp::platform::windows {

// The notification-area icon on the native API (Shell_NotifyIconW with a
// hidden window of its own), as the earlier versions drew it, so the app
// needs no Qt Widgets module. When Explorer starts again (after a crash or a
// restart) it forgets every icon and announces "TaskbarCreated" to top-level
// windows; the icon is added again then. The window procedure runs on the
// GUI thread (the thread that created the window), so the signals are
// emitted there directly.
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
    // Explorer started again: the icon is added again if it was showing.
    void handleTaskbarCreated();

private:
    // NIM_ADD, or NIM_MODIFY when `add` is false.
    [[nodiscard]] bool addIcon(bool add);
    void showMenu();

    // HWND, kept as an integer so this header needs no Windows header.
    quintptr m_window = 0;
    bool m_added = false;
    // Kept to add the icon again after Explorer restarts.
    QString m_tooltip;
};

} // namespace lmp::platform::windows
