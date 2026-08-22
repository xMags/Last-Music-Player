#pragma once

#include "Backend/AccountSessionService.h"
#include "Backend/RemoteMusicService.h"

#include <chrono>
#include <cstdint>

namespace LastMusicPlayer::Backend
{
    // How long a window has to have been left alone before returning to it is
    // treated as a reason to check for remote changes. Without this every
    // alt-tab would be a request to the account service.
    inline constexpr std::chrono::seconds kAutoSyncFocusDebounce{ 30 };

    // How often the running process checks on its own, including while the
    // window is minimized or represented only by the tray icon.
    inline constexpr std::chrono::minutes kAutoSyncPollInterval{ 5 };

    enum class AutoSyncTrigger
    {
        // The user came back to the window.
        WindowFocus,

        // The periodic poll. Its own interval is the debounce, so it is not
        // subject to the focus one.
        Timer,
    };

    // Everything the decision depends on, gathered by the caller so the rule
    // itself touches no services and stays testable.
    struct AutoSyncConditions
    {
        RemoteAccessMode Mode{ RemoteAccessMode::LocalOnly };
        AccountSessionStatus Status{ AccountSessionStatus::SignedOut };

        // Parenthesized so the windows.h "max" macro cannot expand it; this
        // header is reachable from translation units that include windows.h
        // without NOMINMAX.
        std::chrono::steady_clock::duration SinceLastAttempt{
            (std::chrono::steady_clock::duration::max)() };
    };

    // Why a background sync did or did not run. Every skip is named rather
    // than collapsed into a bool so the reason can be asserted in tests and
    // traced when a sync does not happen when expected.
    enum class AutoSyncDecision
    {
        Run,
        NotAccountMode,
        NotSignedIn,
        TooSoon,
    };

    [[nodiscard]] AutoSyncDecision EvaluateAutoSync(
        AutoSyncConditions const& conditions,
        AutoSyncTrigger trigger) noexcept;

    [[nodiscard]] std::chrono::seconds AccountRetryDelay(
        int attemptCount,
        std::uint32_t retryAfterSeconds = 0) noexcept;
}
