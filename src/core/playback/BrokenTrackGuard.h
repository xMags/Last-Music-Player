#pragma once

namespace lmp::core {

// Counts songs that failed one after another, so a queue full of missing or
// unreadable files cannot skip forever. The first and second failure in a
// row skip on; the third stops playback, and counting starts over for the
// next attempt. A song that starts playing resets the count.
//
// Not thread-safe; owned by the player controller.
class BrokenTrackGuard final {
public:
    static constexpr int MaximumConsecutiveFailures = 3;

    // Records one failure. True to skip to the next song, false to stop.
    [[nodiscard]] bool skipAfterFailure() noexcept
    {
        if (++m_failures < MaximumConsecutiveFailures) {
            return true;
        }
        m_failures = 0;
        return false;
    }

    void trackStarted() noexcept { m_failures = 0; }

    [[nodiscard]] int consecutiveFailures() const noexcept { return m_failures; }

private:
    int m_failures = 0;
};

} // namespace lmp::core
