#include "platform/windows/WindowsPowerStatus.h"

#include <windows.h>

namespace lmp::platform::windows {

bool WindowsPowerStatus::onBattery() const
{
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status)) {
        return false;
    }
    // 0: offline (battery), 1: online, 255: unknown.
    return status.ACLineStatus == 0;
}

} // namespace lmp::platform::windows
