#include "core/account/AutoSyncPolicy.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace lmp::core {

AutoSyncDecision evaluateAutoSync(const AutoSyncConditions& conditions, AutoSyncTrigger trigger) noexcept
{
    if (!conditions.accountMode) {
        return AutoSyncDecision::NotAccountMode;
    }
    if (!conditions.sessionValidated) {
        return AutoSyncDecision::NotSignedIn;
    }
    if (trigger == AutoSyncTrigger::WindowFocus && conditions.sinceLastAttempt < AutoSyncFocusDebounce) {
        return AutoSyncDecision::TooSoon;
    }
    return AutoSyncDecision::Run;
}

std::chrono::seconds accountRetryDelay(int attemptCount, std::uint32_t retryAfterSeconds) noexcept
{
    constexpr std::array<std::chrono::seconds, 5> delays{
        std::chrono::seconds{5}, std::chrono::seconds{30}, std::chrono::minutes{2},
        std::chrono::minutes{10}, std::chrono::minutes{30},
    };
    const auto index =
        static_cast<std::size_t>(std::clamp(attemptCount - 1, 0, static_cast<int>(delays.size()) - 1));
    return std::max(delays[index], std::chrono::seconds{retryAfterSeconds});
}

} // namespace lmp::core
