#pragma once

#include <functional>

namespace lmp::platform {

// The operating system's "reduce animations" preference.
class IReducedMotion {
public:
    using ChangeHandler = std::function<void(bool reduced)>;

    virtual ~IReducedMotion() = default;

    // True when the user turned animation effects off.
    [[nodiscard]] virtual bool reduced() const = 0;

    // Called on the thread that owns the implementation whenever the
    // preference changes. Replaces any earlier handler; an empty handler
    // stops notifications.
    virtual void setChangeHandler(ChangeHandler handler) = 0;

protected:
    IReducedMotion() = default;
    IReducedMotion(const IReducedMotion&) = default;
    IReducedMotion& operator=(const IReducedMotion&) = default;
};

} // namespace lmp::platform
