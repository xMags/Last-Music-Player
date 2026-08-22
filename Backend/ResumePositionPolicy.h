#pragma once

namespace LastMusicPlayer::Backend
{
    // A stop only counts as somewhere to resume from when there is a
    // meaningful amount both behind and ahead of it. Without the head margin
    // every track glanced at for a few seconds would offer to resume from
    // nowhere; without the tail margin a track abandoned during its outro
    // would offer to resume from its last few seconds.
    inline constexpr double kResumeMinimumElapsedSeconds = 30.0;
    inline constexpr double kResumeMinimumRemainingSeconds = 30.0;

    // Returns the position worth storing for `positionSeconds`, or 0 to clear
    // any stored one. A duration of zero means the length is not known yet, in
    // which case only the head margin can be checked.
    [[nodiscard]] inline double ResumePositionToStore(
        double positionSeconds,
        double durationSeconds) noexcept
    {
        if (positionSeconds < kResumeMinimumElapsedSeconds)
        {
            return 0.0;
        }
        if (durationSeconds > 0.0
            && positionSeconds > durationSeconds - kResumeMinimumRemainingSeconds)
        {
            return 0.0;
        }
        return positionSeconds;
    }

    // Whether a stored position should actually be offered. Guards against a
    // position saved before a track was replaced by a shorter file.
    [[nodiscard]] inline bool CanResumeFrom(
        double storedSeconds,
        double durationSeconds) noexcept
    {
        if (storedSeconds < kResumeMinimumElapsedSeconds)
        {
            return false;
        }
        return durationSeconds <= 0.0
            || storedSeconds <= durationSeconds - kResumeMinimumRemainingSeconds;
    }
}
