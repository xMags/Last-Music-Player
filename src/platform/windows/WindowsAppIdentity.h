#pragma once

#include <QString>

namespace lmp::platform::windows {

// Gives the process the explicit AppUserModelID that the installer writes
// into the Start menu shortcut, so Windows shows the app's name and icon for
// its media session and taskbar entry. Must run before any window exists.
class WindowsAppIdentity {
public:
    // Returns false (and logs why) when Windows refuses; the app still runs.
    [[nodiscard]] static bool apply(const QString& appUserModelId);
};

} // namespace lmp::platform::windows
