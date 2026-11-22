#pragma once

#include <QString>

#include <cstdint>

namespace lmp::core {

// Decides when a playback has lasted long enough to count as a play in the
// history: 8 seconds of time actually spent playing the same item. The
// caller samples it with a monotonic clock while playing; each sample counts
// at most one second, so a stalled timer or a long pause cannot add time.
//
// Not thread-safe; owned and sampled by one thread.
class PlaybackHistoryQualifier final {
public:
    static constexpr double RequiredPlaybackSeconds = 8.0;
    static constexpr std::uint64_t MaximumSampleMilliseconds = 1000;

    // Starts counting for `identity` (for example owner and track key).
    void reset(QString identity);
    // Forgets the current item; nothing qualifies until the next reset.
    void clear() noexcept;

    // One sample. True when the played time has reached the required amount
    // (and keeps returning true until markCompleted). A sample for another
    // identity, an empty identity or after completion counts nothing. The
    // first sample after a reset or a pause only sets the starting point.
    bool observe(const QString& identity, bool playing, std::uint64_t tickMilliseconds) noexcept;

    // The play was recorded; later samples never qualify again.
    void markCompleted() noexcept { m_completed = true; }

    [[nodiscard]] double qualifiedSeconds() const noexcept { return m_qualifiedSeconds; }
    [[nodiscard]] bool completed() const noexcept { return m_completed; }

private:
    QString m_identity;
    double m_qualifiedSeconds = 0.0;
    std::uint64_t m_lastPlayingTickMilliseconds = 0;
    bool m_completed = false;
};

} // namespace lmp::core
