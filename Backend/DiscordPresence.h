#pragma once
#include <windows.h>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace LastMusicPlayer::Backend
{
    // Rich-presence payload built per track. Empty title clears the activity.
    struct PresencePayload
    {
        std::wstring title;
        std::wstring artist;
        std::wstring album;          // kept for future use; not currently emitted
        std::wstring artworkUrl;     // HTTPS only; empty -> omit large_image
        double durationSeconds{ 0.0 };
        double positionSeconds{ 0.0 };
        bool isPlaying{ true };
        bool isLocal{ false };       // drives the "Source: Local/Remote" label
    };

    // Discord Rich Presence client over the IPC named pipe
    // (\\.\pipe\discord-ipc-N). All IPC runs on a single worker so UI
    // callbacks never race a pipe close/reconnect or block on Discord.
    class DiscordPresence
    {
    public:
        DiscordPresence() = default;
        ~DiscordPresence();

        DiscordPresence(DiscordPresence const&) = delete;
        DiscordPresence& operator=(DiscordPresence const&) = delete;

        bool Connect();
        void Disconnect();
        bool IsConnected() const;

        void SetNowPlaying(PresencePayload const& payload);
        // durationSeconds <= 0 keeps the cached value unchanged. Pass the
        // live MediaPlaybackSession.NaturalDuration when you have it so
        // late-resolved durations (the track was still opening when
        // SetNowPlaying ran) flow into the next activity send.
        void SetPlaybackState(bool isPlaying, double positionSeconds, double durationSeconds = -1.0);
        void SetPosition(double positionSeconds);
        // Late duration update — fires from MediaPlaybackSession's
        // NaturalDurationChanged event. Triggers a re-send so the
        // progress bar can finally appear.
        void SetDuration(double durationSeconds);
        // Updates the artwork field of the cached payload (set asynchronously
        // after the provider service has converted an HTTPS URL to a Discord
        // External Asset `mp:external/...` proxy URL) and re-emits the activity.
        // `proxyUrl` must already be in the form Discord accepts.
        void SetArtworkProxyUrl(
            std::wstring const& proxyUrl,
            std::wstring const& originalTitle,
            std::wstring const& originalArtist);
        void Clear();

    private:
        enum class FrameReadResult
        {
            NoFrame,
            Frame,
            Disconnected,
        };

        void EnsureWorkerLocked();
        void IoMain();
        bool OpenAndHandshake();
        void ClosePipe();
        bool WriteFrame(int opcode, std::string const& payload);
        FrameReadResult TryReadFrame(int& opcode, std::string& payload);
        bool DrainIncomingFrames();
        void HandleIncomingFrame(int opcode, std::string const& payload);
        void MarkDisconnected();
        void ScheduleReconnect();
        std::string BuildActivityJson(PresencePayload const& p, std::string const& nonce) const;
        static std::string BuildActivityFingerprint(PresencePayload const& p);
        static std::string BuildClearJson(std::string const& nonce);
        static std::string NextNonce(uint64_t sequence);

        // Owned exclusively by IoMain. Public callers observe m_ready under
        // m_mutex rather than touching the HANDLE directly.
        HANDLE m_pipe{ INVALID_HANDLE_VALUE };

        mutable std::mutex m_mutex;
        std::condition_variable m_wake;
        std::thread m_ioThread;
        bool m_workerStarted{ false };
        bool m_stopping{ false };
        bool m_connectRequested{ false };
        bool m_ready{ false };
        bool m_clearRequested{ false };
        std::optional<PresencePayload> m_last;
        std::string m_lastAcknowledgedFingerprint;
        std::chrono::steady_clock::time_point m_lastSendAt{};
        std::chrono::steady_clock::time_point m_nextReconnectAt{};
        std::chrono::milliseconds m_reconnectDelay{ 1000 };
        bool m_waitingForReply{ false };
        bool m_waitingForClear{ false };
        std::string m_waitingNonce;
        std::string m_waitingFingerprint;
        std::chrono::steady_clock::time_point m_replyDeadline{};
        uint64_t m_nextNonce{ 0 };
    };
}
