#pragma once
#include "TrackInfo.h"
#include <vector>
#include <functional>

namespace LastMusicPlayer::Backend
{
    enum class RepeatMode
    {
        Off,
        All,
        One
    };

    class PlaylistManager
    {
    public:
        PlaylistManager() = default;
        ~PlaylistManager() = default;

        // Queue management
        void SetQueue(const std::vector<TrackInfo>& tracks);
        void AddToQueue(const TrackInfo& track);
        void RemoveFromQueue(size_t index);
        void ClearQueue();

        // Navigation
        TrackInfo GetCurrentTrack() const;
        TrackInfo Next();
        TrackInfo Previous();

        // Modes
        void SetShuffle(bool enabled);
        bool GetShuffle() const;

        void SetRepeatMode(RepeatMode mode);
        RepeatMode GetRepeatMode() const;

        // Queue info
        size_t GetCurrentIndex() const;
        size_t GetQueueSize() const;
        const std::vector<TrackInfo>& GetQueue() const;

        // Callback for queue changes
        using QueueChangedCallback = std::function<void()>;
        void OnQueueChanged(QueueChangedCallback callback);

    private:
        std::vector<TrackInfo> m_queue;
        size_t m_currentIndex{ 0 };
        bool m_shuffle{ false };
        RepeatMode m_repeatMode{ RepeatMode::Off };

        QueueChangedCallback m_queueChangedCallback;
    };
}
