#include "pch.h"

#include "Backend/AutoSyncPolicy.h"

#include <algorithm>
#include <iterator>

namespace LastMusicPlayer::Backend
{
    AutoSyncDecision EvaluateAutoSync(
        AutoSyncConditions const& conditions,
        AutoSyncTrigger trigger) noexcept
    {
        if (conditions.Mode != RemoteAccessMode::Account)
        {
            return AutoSyncDecision::NotAccountMode;
        }

        // Offline deliberately does not qualify. MusicSyncService refuses to
        // sync an offline session and would only record a failure, so polling
        // one buys nothing; the session returns to Validated on its own and
        // the next trigger picks it up.
        if (conditions.Status != AccountSessionStatus::Validated)
        {
            return AutoSyncDecision::NotSignedIn;
        }
        if (trigger == AutoSyncTrigger::WindowFocus
            && conditions.SinceLastAttempt < kAutoSyncFocusDebounce)
        {
            return AutoSyncDecision::TooSoon;
        }

        return AutoSyncDecision::Run;
    }

    std::chrono::seconds AccountRetryDelay(
        int attemptCount,
        std::uint32_t retryAfterSeconds) noexcept
    {
        constexpr std::chrono::seconds delays[]{
            std::chrono::seconds{ 5 },
            std::chrono::seconds{ 30 },
            std::chrono::minutes{ 2 },
            std::chrono::minutes{ 10 },
            std::chrono::minutes{ 30 },
        };
        auto const index = static_cast<std::size_t>((std::clamp)(
            attemptCount - 1,
            0,
            static_cast<int>(std::size(delays) - 1)));
        return (std::max)(delays[index], std::chrono::seconds{ retryAfterSeconds });
    }
}
