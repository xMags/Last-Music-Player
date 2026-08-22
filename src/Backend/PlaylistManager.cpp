#include "pch.h"
#include "Backend/PlaylistManager.h"
#include <algorithm>
#include <random>

namespace LastMusicPlayer::Backend
{
    void PlaylistManager::SetQueue(const std::vector<TrackInfo>& tracks)
    {
        m_queue = tracks;
        m_currentIndex = 0;
        if (m_queueChangedCallback) m_queueChangedCallback();
    }

    void PlaylistManager::AddToQueue(const TrackInfo& track)
    {
        m_queue.push_back(track);
        if (m_queueChangedCallback) m_queueChangedCallback();
    }

    void PlaylistManager::RemoveFromQueue(size_t index)
    {
        if (index < m_queue.size())
        {
            m_queue.erase(m_queue.begin() + index);
            if (m_currentIndex >= m_queue.size() && m_currentIndex > 0)
                m_currentIndex = m_queue.size() - 1;
            if (m_queueChangedCallback) m_queueChangedCallback();
        }
    }

    void PlaylistManager::ClearQueue()
    {
        m_queue.clear();
        m_currentIndex = 0;
        if (m_queueChangedCallback) m_queueChangedCallback();
    }

    TrackInfo PlaylistManager::GetCurrentTrack() const
    {
        if (m_queue.empty()) return nullptr;
        return m_queue[m_currentIndex];
    }

    TrackInfo PlaylistManager::Next()
    {
        if (m_queue.empty()) return nullptr;

        if (m_repeatMode == RepeatMode::One)
            return m_queue[m_currentIndex];

        if (m_shuffle)
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<size_t> dist(0, m_queue.size() - 1);
            m_currentIndex = dist(gen);
        }
        else
        {
            m_currentIndex++;
            if (m_currentIndex >= m_queue.size())
            {
                m_currentIndex = (m_repeatMode == RepeatMode::All) ? 0 : m_queue.size() - 1;
            }
        }

        if (m_queueChangedCallback) m_queueChangedCallback();
        return m_queue[m_currentIndex];
    }

    TrackInfo PlaylistManager::Previous()
    {
        if (m_queue.empty()) return nullptr;

        if (m_currentIndex > 0)
            m_currentIndex--;
        else if (m_repeatMode == RepeatMode::All)
            m_currentIndex = m_queue.size() - 1;

        if (m_queueChangedCallback) m_queueChangedCallback();
        return m_queue[m_currentIndex];
    }

    void PlaylistManager::SetShuffle(bool enabled) { m_shuffle = enabled; }
    bool PlaylistManager::GetShuffle() const { return m_shuffle; }

    void PlaylistManager::SetRepeatMode(RepeatMode mode) { m_repeatMode = mode; }
    RepeatMode PlaylistManager::GetRepeatMode() const { return m_repeatMode; }

    size_t PlaylistManager::GetCurrentIndex() const { return m_currentIndex; }
    size_t PlaylistManager::GetQueueSize() const { return m_queue.size(); }
    const std::vector<TrackInfo>& PlaylistManager::GetQueue() const { return m_queue; }

    void PlaylistManager::OnQueueChanged(QueueChangedCallback callback)
    {
        m_queueChangedCallback = std::move(callback);
    }
}
