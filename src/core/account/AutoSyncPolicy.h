#pragma once

#include <chrono>
#include <cstdint>

namespace lmp::core {

// How long a window must have been left alone before coming back to it is a
// reason to check for remote changes. Without it every window switch would be
// a request to the account service.
inline constexpr std::chrono::seconds AutoSyncFocusDebounce{30};

// How often the running app checks on its own, also while the window is
// minimised or only in the tray.
inline constexpr std::chrono::minutes AutoSyncPollInterval{5};

enum class AutoSyncTrigger {
    // The user came back to the window.
    WindowFocus,
    // The periodic poll. Its interval is its own debounce, so the focus
    // debounce does not apply to it.
    Timer,
};

// What the decision depends on, gathered by the caller so the rule touches no
// services.
struct AutoSyncConditions {
    // The app is in Account mode.
    bool accountMode = false;
    // The account session is signed in and validated. Signing in, signed
    // out and offline sessions all count as false: a sync of an offline
    // session is refused and would only record a failure, and the session
    // becomes validated again on its own.
    bool sessionValidated = false;
    std::chrono::steady_clock::duration sinceLastAttempt = std::chrono::steady_clock::duration::max();
};

// Why a background sync did or did not run. Every skip has its own reason so
// it can be checked in tests and traced in logs.
enum class AutoSyncDecision {
    Run,
    NotAccountMode,
    NotSignedIn,
    TooSoon,
};

[[nodiscard]] AutoSyncDecision evaluateAutoSync(const AutoSyncConditions& conditions, AutoSyncTrigger trigger) noexcept;

// Delay before retry number `attemptCount` (1 for the first retry) of a failed
// account request: 5 s, 30 s, 2 min, 10 min, then 30 min for every later
// attempt. A longer Retry-After from the server wins.
[[nodiscard]] std::chrono::seconds accountRetryDelay(int attemptCount, std::uint32_t retryAfterSeconds = 0) noexcept;

} // namespace lmp::core
