#pragma once

namespace lmp::platform {

// Whether the machine runs on battery, as the operating system reports it.
class IPowerStatus {
public:
    virtual ~IPowerStatus() = default;

    // No mains power is connected.
    [[nodiscard]] virtual bool onBattery() const = 0;

protected:
    IPowerStatus() = default;
    IPowerStatus(const IPowerStatus&) = default;
    IPowerStatus& operator=(const IPowerStatus&) = default;
};

} // namespace lmp::platform
