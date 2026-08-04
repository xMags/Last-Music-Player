#pragma once

#include "Backend/AccountSessionService.h"
#include "Backend/RemoteMusicService.h"

#include <chrono>

namespace LastMusicPlayer::Backend
{
    // How long a window has to have been left alone before returning to it is
    // treated as a reason to check for remote changes. Without this every
    // alt-tab would be a request to the account service.
    inline constexpr std::chrono::minutes kAutoSyncFocusDebounce{ 5 };

    // How often an open, focused window checks on its own. Long enough that an
    // app left running all day is not a meaningful load, short enough that a
    // change made on another device shows up in the same sitting.
    inline constexpr std::chrono::minutes kAutoSyncPollInterval{ 15 };

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
        bool Enabled{};
        RemoteAccessMode Mode{ RemoteAccessMode::LocalOnly };
        AccountSessionStatus Status{ AccountSessionStatus::SignedOut };
        bool Syncing{};

        // Visible, not minimized, and not hidden to the tray.
        bool WindowInteractive{};

        // An account playlist detail view is on screen.
        bool AccountDetailOpen{};

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
        Disabled,
        NotAccountMode,
        NotSignedIn,
        AlreadySyncing,
        WindowNotInteractive,
        AccountDetailOpen,
        TooSoon,
    };

    [[nodiscard]] AutoSyncDecision EvaluateAutoSync(
        AutoSyncConditions const& conditions,
        AutoSyncTrigger trigger) noexcept;
}
