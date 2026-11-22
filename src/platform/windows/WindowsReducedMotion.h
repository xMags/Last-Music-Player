#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>

#include "platform/IReducedMotion.h"

namespace lmp::platform::windows {

// Reads the Windows "Animation effects" setting and reports changes. Windows
// announces a change with WM_SETTINGCHANGE to every top-level window, so this
// watches the application's native events. Lives on the GUI thread; the
// handler runs there too.
class WindowsReducedMotion final : public IReducedMotion, public QAbstractNativeEventFilter {
public:
    WindowsReducedMotion();
    ~WindowsReducedMotion() override;

    WindowsReducedMotion(const WindowsReducedMotion&) = delete;
    WindowsReducedMotion& operator=(const WindowsReducedMotion&) = delete;

    [[nodiscard]] bool reduced() const override;
    void setChangeHandler(ChangeHandler handler) override;

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

private:
    [[nodiscard]] static bool readSystemSetting();

    bool m_reduced = false;
    ChangeHandler m_handler;
};

} // namespace lmp::platform::windows
