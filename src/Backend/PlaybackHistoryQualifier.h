#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

namespace LastMusicPlayer::Backend
{
    class PlaybackHistoryQualifier final
    {
    public:
        static constexpr double RequiredPlaybackSeconds = 8.0;
        static constexpr std::uint64_t MaximumSampleMilliseconds = 1000;

        void Reset(std::wstring identity)
        {
            m_identity = std::move(identity);
            m_qualifiedSeconds = 0.0;
            m_lastPlayingTickMilliseconds = 0;
            m_completed = false;
        }

        void Clear() noexcept
        {
            m_identity.clear();
            m_qualifiedSeconds = 0.0;
            m_lastPlayingTickMilliseconds = 0;
            m_completed = false;
        }

        bool Observe(
            std::wstring const& identity,
            bool playing,
            std::uint64_t tickMilliseconds) noexcept
        {
            if (m_completed || identity.empty() || identity != m_identity)
            {
                return false;
            }

            if (!playing)
            {
                m_lastPlayingTickMilliseconds = 0;
                return false;
            }

            if (m_lastPlayingTickMilliseconds == 0 || tickMilliseconds <= m_lastPlayingTickMilliseconds)
            {
                m_lastPlayingTickMilliseconds = tickMilliseconds;
                return false;
            }

            auto elapsedMilliseconds = (std::min)(
                tickMilliseconds - m_lastPlayingTickMilliseconds,
                MaximumSampleMilliseconds);
            m_lastPlayingTickMilliseconds = tickMilliseconds;
            m_qualifiedSeconds += static_cast<double>(elapsedMilliseconds) / 1000.0;
            return m_qualifiedSeconds >= RequiredPlaybackSeconds;
        }

        void MarkCompleted() noexcept
        {
            m_completed = true;
        }

        double QualifiedSeconds() const noexcept
        {
            return m_qualifiedSeconds;
        }

        bool Completed() const noexcept
        {
            return m_completed;
        }

    private:
        std::wstring m_identity;
        double m_qualifiedSeconds{};
        std::uint64_t m_lastPlayingTickMilliseconds{};
        bool m_completed{};
    };
}
