#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "core/playback/PlaybackClock.h"

// A clock for the player controller tests: time moves only when the test
// calls advance(), which fires the timers that fall due on the way.
class FakePlaybackClock final : public lmp::core::PlaybackClock {
public:
    [[nodiscard]] std::uint64_t nowMilliseconds() const override { return m_now; }

    [[nodiscard]] std::unique_ptr<lmp::core::PlaybackTimer> createTimer(std::function<void()> onTimeout) override
    {
        auto timer = std::make_unique<Timer>(*this, std::move(onTimeout));
        m_timers.push_back(timer->state);
        return timer;
    }

    // Moves time forward, firing every timer that falls due on the way, in order.
    void advance(std::uint64_t milliseconds)
    {
        const std::uint64_t target = m_now + milliseconds;
        for (;;) {
            std::shared_ptr<TimerState> due;
            for (const std::weak_ptr<TimerState>& weak : m_timers) {
                const std::shared_ptr<TimerState> timer = weak.lock();
                if (timer && timer->active && timer->deadline <= target
                    && (!due || timer->deadline < due->deadline)) {
                    due = timer;
                }
            }
            if (!due) {
                break;
            }
            m_now = std::max(m_now, due->deadline);
            due->active = false;
            due->callback();
        }
        m_now = target;
    }

private:
    struct TimerState {
        std::function<void()> callback;
        bool active = false;
        std::uint64_t deadline = 0;
    };

    class Timer final : public lmp::core::PlaybackTimer {
    public:
        Timer(FakePlaybackClock& clock, std::function<void()> onTimeout)
            : state(std::make_shared<TimerState>())
            , m_clock(clock)
        {
            state->callback = std::move(onTimeout);
        }
        void start(std::chrono::milliseconds delay) override
        {
            state->active = true;
            state->deadline = m_clock.m_now + static_cast<std::uint64_t>(delay.count());
        }
        void stop() override { state->active = false; }
        [[nodiscard]] bool isActive() const override { return state->active; }

        std::shared_ptr<TimerState> state;

    private:
        FakePlaybackClock& m_clock;
    };

    // Not 0: the history qualifier reads 0 as "no starting point yet".
    std::uint64_t m_now = 1'000'000;
    std::vector<std::weak_ptr<TimerState>> m_timers;
};
