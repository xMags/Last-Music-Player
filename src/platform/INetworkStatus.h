#pragma once

#include <functional>

namespace lmp::platform {

// The machine's internet connection, as the operating system reports it.
class INetworkStatus {
public:
    // Called with the new values when the connection changes.
    using ChangeHandler = std::function<void(bool online, bool metered)>;

    virtual ~INetworkStatus() = default;

    // The connection reaches the internet.
    [[nodiscard]] virtual bool online() const = 0;
    // The connection costs money per use: a fixed or variable plan, roaming,
    // or over its data limit.
    [[nodiscard]] virtual bool metered() const = 0;

    // Called on the thread that owns the implementation whenever the
    // connection changes. Replaces any earlier handler; an empty handler
    // stops notifications.
    virtual void setChangeHandler(ChangeHandler handler) = 0;

protected:
    INetworkStatus() = default;
    INetworkStatus(const INetworkStatus&) = default;
    INetworkStatus& operator=(const INetworkStatus&) = default;
};

} // namespace lmp::platform
