#include "core/playback/MediaControlsThrottle.h"

namespace lmp::core {

bool MediaControlsThrottle::admit(bool force)
{
    const std::uint64_t now = m_clock.nowMilliseconds();
    const auto interval = static_cast<std::uint64_t>(ReportInterval.count());
    if (!force && m_reported && now - m_lastReport < interval) {
        return false;
    }
    m_reported = true;
    m_lastReport = now;
    return true;
}

} // namespace lmp::core
