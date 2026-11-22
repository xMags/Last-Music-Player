#pragma once

#include <chrono>
#include <cstdint>

#include "core/playback/PlaybackClock.h"

namespace lmp::core {

// How often the playback position goes to the system's media controls. A
// forced report (the song or its play state changed) always goes out at
// once; any other report only when the last one, forced or not, is at
// least ReportInterval old. A seek is not forced: the system display
// extrapolates the position while playing, so a jump shows on the next
// report.
class MediaControlsThrottle {
public:
    static constexpr std::chrono::milliseconds ReportInterval{5000};

    explicit MediaControlsThrottle(const PlaybackClock& clock) : m_clock(clock) {}

    // True when a report should go out now; it then counts as the last one.
    [[nodiscard]] bool admit(bool force);

private:
    const PlaybackClock& m_clock;
    bool m_reported = false;
    std::uint64_t m_lastReport = 0;
};

} // namespace lmp::core
