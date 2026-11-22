#include "core/playback/PlaybackHistoryQualifier.h"

#include <algorithm>
#include <utility>

namespace lmp::core {

void PlaybackHistoryQualifier::reset(QString identity)
{
    m_identity = std::move(identity);
    m_qualifiedSeconds = 0.0;
    m_lastPlayingTickMilliseconds = 0;
    m_completed = false;
}

void PlaybackHistoryQualifier::clear() noexcept
{
    m_identity.clear();
    m_qualifiedSeconds = 0.0;
    m_lastPlayingTickMilliseconds = 0;
    m_completed = false;
}

bool PlaybackHistoryQualifier::observe(const QString& identity, bool playing, std::uint64_t tickMilliseconds) noexcept
{
    if (m_completed || identity.isEmpty() || identity != m_identity) {
        return false;
    }

    if (!playing) {
        m_lastPlayingTickMilliseconds = 0;
        return false;
    }

    if (m_lastPlayingTickMilliseconds == 0 || tickMilliseconds <= m_lastPlayingTickMilliseconds) {
        m_lastPlayingTickMilliseconds = tickMilliseconds;
        return false;
    }

    const std::uint64_t elapsedMilliseconds =
        std::min(tickMilliseconds - m_lastPlayingTickMilliseconds, MaximumSampleMilliseconds);
    m_lastPlayingTickMilliseconds = tickMilliseconds;
    m_qualifiedSeconds += static_cast<double>(elapsedMilliseconds) / 1000.0;
    return m_qualifiedSeconds >= RequiredPlaybackSeconds;
}

} // namespace lmp::core
