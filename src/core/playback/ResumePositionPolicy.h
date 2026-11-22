#pragma once

namespace lmp::core {

// A stop only counts as somewhere to resume from when a meaningful amount is
// both behind and ahead of it. Without the head margin every track glanced at
// for a few seconds would offer to resume; without the tail margin a track
// left during its outro would offer to resume from its last seconds.
inline constexpr double ResumeMinimumElapsedSeconds = 30.0;
inline constexpr double ResumeMinimumRemainingSeconds = 30.0;

// The position worth storing for `positionSeconds`, or 0 to clear any stored
// one. A duration of zero means the length is not known yet, so only the
// head margin can be checked.
[[nodiscard]] constexpr double resumePositionToStore(double positionSeconds, double durationSeconds) noexcept
{
    if (positionSeconds < ResumeMinimumElapsedSeconds) {
        return 0.0;
    }
    if (durationSeconds > 0.0 && positionSeconds > durationSeconds - ResumeMinimumRemainingSeconds) {
        return 0.0;
    }
    return positionSeconds;
}

// Whether a stored position should be offered. Guards against a position
// saved before the track's file was replaced by a shorter one.
[[nodiscard]] constexpr bool canResumeFrom(double storedSeconds, double durationSeconds) noexcept
{
    if (storedSeconds < ResumeMinimumElapsedSeconds) {
        return false;
    }
    return durationSeconds <= 0.0 || storedSeconds <= durationSeconds - ResumeMinimumRemainingSeconds;
}

} // namespace lmp::core
