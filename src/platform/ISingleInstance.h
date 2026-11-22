#pragma once

namespace lmp::platform {

// Keeps the application to one running copy per user session.
class ISingleInstance {
public:
    virtual ~ISingleInstance() = default;

    // True when this process is the only running copy. The claim is held
    // until the object is destroyed.
    [[nodiscard]] virtual bool tryAcquire() = 0;

    // Brings the window of the copy that is already running to the front,
    // restoring it when minimised. Does nothing when no such window exists.
    virtual void activateExistingWindow() = 0;

protected:
    ISingleInstance() = default;
    ISingleInstance(const ISingleInstance&) = default;
    ISingleInstance& operator=(const ISingleInstance&) = default;
};

} // namespace lmp::platform
