#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace lmp::core {

// A single-shot timer made by a PlaybackClock. start() while it is running
// starts it again from now; stop() cancels it. The callback runs on the
// thread that owns the timer and never after the timer is destroyed, so an
// owner that keeps the timer as a member may capture itself in the callback.
class PlaybackTimer {
public:
    virtual ~PlaybackTimer() = default;

    virtual void start(std::chrono::milliseconds delay) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool isActive() const = 0;

protected:
    PlaybackTimer() = default;
    PlaybackTimer(const PlaybackTimer&) = default;
    PlaybackTimer& operator=(const PlaybackTimer&) = default;
};

// Time for the player: a monotonic millisecond count and delayed callbacks.
// Injected so tests can move time forward without waiting.
class PlaybackClock {
public:
    virtual ~PlaybackClock() = default;

    // Milliseconds on a monotonic clock; never 0, never goes back.
    [[nodiscard]] virtual std::uint64_t nowMilliseconds() const = 0;
    [[nodiscard]] virtual std::unique_ptr<PlaybackTimer> createTimer(std::function<void()> onTimeout) = 0;

protected:
    PlaybackClock() = default;
    PlaybackClock(const PlaybackClock&) = default;
    PlaybackClock& operator=(const PlaybackClock&) = default;
};

// The real clock: QElapsedTimer for the time and QTimer for the callbacks.
// Its timers belong to the thread that creates them.
class SystemPlaybackClock final : public PlaybackClock {
public:
    [[nodiscard]] std::uint64_t nowMilliseconds() const override;
    [[nodiscard]] std::unique_ptr<PlaybackTimer> createTimer(std::function<void()> onTimeout) override;
};

} // namespace lmp::core
