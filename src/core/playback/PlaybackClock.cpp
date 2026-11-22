#include "core/playback/PlaybackClock.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <utility>

namespace lmp::core {

namespace {

class QtPlaybackTimer final : public PlaybackTimer {
public:
    explicit QtPlaybackTimer(std::function<void()> onTimeout)
        : m_onTimeout(std::move(onTimeout))
    {
        m_timer.setSingleShot(true);
        m_timer.setTimerType(Qt::PreciseTimer);
        // The timer is the context: the connection dies with it.
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this]() {
            if (m_onTimeout) {
                m_onTimeout();
            }
        });
    }

    void start(std::chrono::milliseconds delay) override { m_timer.start(delay); }
    void stop() override { m_timer.stop(); }
    [[nodiscard]] bool isActive() const override { return m_timer.isActive(); }

private:
    std::function<void()> m_onTimeout;
    QTimer m_timer;
};

} // namespace

std::uint64_t SystemPlaybackClock::nowMilliseconds() const
{
    QElapsedTimer timer;
    timer.start();
    // Counted from the system's monotonic reference (boot on Windows), so it
    // is never 0 in practice; the + 1 keeps that true everywhere.
    const qint64 now = timer.msecsSinceReference();
    return now > 0 ? static_cast<std::uint64_t>(now) + 1 : 1;
}

std::unique_ptr<PlaybackTimer> SystemPlaybackClock::createTimer(std::function<void()> onTimeout)
{
    return std::make_unique<QtPlaybackTimer>(std::move(onTimeout));
}

} // namespace lmp::core
