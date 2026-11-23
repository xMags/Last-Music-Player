#pragma once

#include "platform/IPowerStatus.h"

namespace lmp::platform::windows {

// The power source Windows reports (GetSystemPowerStatus): on battery when
// mains power is not connected. A machine that cannot tell counts as on
// mains, so downloads are not held back by an unknown state.
class WindowsPowerStatus final : public IPowerStatus {
public:
    [[nodiscard]] bool onBattery() const override;
};

} // namespace lmp::platform::windows
