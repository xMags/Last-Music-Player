#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Networking.Sockets.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.System.Threading.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    struct CastDevice
    {
        std::wstring id;
        std::wstring name;
        std::wstring host;
        uint16_t port{ 8009 };
    };

    // Self-contained Cast v2 discovery and control client. No local sidecar is required.
    class CastEngine
    {
    public:
        CastEngine() = default;
        ~CastEngine();

        CastEngine(CastEngine const&) = delete;
        CastEngine& operator=(CastEngine const&) = delete;

        // Blocking DNS-SD browse for up to timeoutMs. Call OFF the UI thread.
        std::vector<CastDevice> Discover(uint32_t timeoutMs);

        winrt::Windows::Foundation::IAsyncAction ConnectAsync(winrt::hstring host);
        winrt::Windows::Foundation::IAsyncAction LoadAsync(
            winrt::hstring url,
            winrt::hstring title,
            winrt::hstring artist,
            winrt::hstring artworkUrl);
        void Play();
        void Pause();
        void Stop();
        void Seek(double seconds);
        void RequestStatus();
        void SetVolume(double level);
        void Disconnect();

        bool IsConnected() const { return m_connected.load(); }
        bool IsMediaChannelOpen() const;
        winrt::hstring CurrentHost() const;

        // Invoked from a background thread; the consumer marshals to the UI.
        std::function<void(winrt::hstring /*playerState*/, double /*current*/, double /*duration*/)> StatusChanged;
        std::function<void()> Ended;
        std::function<void()> Disconnected;

    private:
        winrt::Windows::Foundation::IAsyncAction ReadLoopAsync();
        winrt::fire_and_forget PumpOutboxAsync();
        void SendJson(std::string const& ns, std::string const& destination, std::string const& jsonPayload);
        void EnqueueFrame(std::vector<uint8_t> frame);
        void HandleMessage(std::string const& ns, std::string const& payloadUtf8);
        void StartHeartbeat();
        void TeardownInternal(bool notify);
        int NextRequestId() { return ++m_requestId; }

        winrt::Windows::Networking::Sockets::StreamSocket m_socket{ nullptr };
        winrt::Windows::Storage::Streams::DataWriter m_writer{ nullptr };
        winrt::Windows::Storage::Streams::DataReader m_reader{ nullptr };
        winrt::Windows::System::Threading::ThreadPoolTimer m_heartbeat{ nullptr };

        std::atomic<bool> m_connected{ false };
        std::atomic<bool> m_running{ false };
        std::atomic<bool> m_pumpRunning{ false };

        std::mutex m_outboxMutex;
        std::deque<std::vector<uint8_t>> m_outbox;

        mutable std::mutex m_stateMutex;
        winrt::hstring m_host;
        std::string m_transportId;   // media-channel destination
        std::string m_sessionId;
        int m_mediaSessionId{ 0 };
        std::atomic<int> m_requestId{ 0 };
        bool m_mediaChannelOpen{ false };
        bool m_loadSent{ false };
        std::string m_pendingLoadJson;
    };
}
