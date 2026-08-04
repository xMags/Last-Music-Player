#include "pch.h"

#include "Backend/AutoSyncPolicy.h"

namespace LastMusicPlayer::Backend
{
    AutoSyncDecision EvaluateAutoSync(
        AutoSyncConditions const& conditions,
        AutoSyncTrigger trigger) noexcept
    {
        if (!conditions.Enabled)
        {
            return AutoSyncDecision::Disabled;
        }
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
        if (conditions.Syncing)
        {
            return AutoSyncDecision::AlreadySyncing;
        }

        // Closing to the tray leaves the process running. An app the user
        // cannot see has no reason to keep talking to the account service.
        if (!conditions.WindowInteractive)
        {
            return AutoSyncDecision::WindowNotInteractive;
        }

        // A sync invalidates the bindings behind an open account playlist, so
        // the interactive path closes that view first. A background sync must
        // not close anything, so it waits for the next trigger instead of
        // changing the world underneath a view the user is reading.
        if (conditions.AccountDetailOpen)
        {
            return AutoSyncDecision::AccountDetailOpen;
        }

        if (trigger == AutoSyncTrigger::WindowFocus
            && conditions.SinceLastAttempt < kAutoSyncFocusDebounce)
        {
            return AutoSyncDecision::TooSoon;
        }

        return AutoSyncDecision::Run;
    }
}
