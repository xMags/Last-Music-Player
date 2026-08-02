#include "pch.h"
#include "Backend/DiscordPresence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

// The Discord application client id is build-private. Official builds define it
// in a gitignored Backend/AppSecrets.local.h; public builds compile with the
// empty default below, which disables Discord Rich Presence. Copy
// Backend/AppSecrets.example.h to AppSecrets.local.h and fill it in to enable.
#if !defined(LMP_DISCORD_CLIENT_ID) && __has_include("Backend/AppSecrets.local.h")
#include "Backend/AppSecrets.local.h"
#endif
#ifndef LMP_DISCORD_CLIENT_ID
#define LMP_DISCORD_CLIENT_ID ""
#endif
#ifndef LMP_DISCORD_IPC_PIPE_PREFIX
#define LMP_DISCORD_IPC_PIPE_PREFIX L"\\\\.\\pipe\\discord-ipc-"
#endif

namespace
{
    constexpr char kDiscordClientId[] = LMP_DISCORD_CLIENT_ID;
    constexpr std::chrono::seconds kMinSendGap{ 4 };
    constexpr std::chrono::seconds kReplyTimeout{ 3 };
    constexpr std::chrono::seconds kHandshakeTimeout{ 3 };
    constexpr std::chrono::milliseconds kPipeBusyWait{ 250 };
    constexpr std::chrono::milliseconds kIdlePoll{ 25 };
    constexpr std::chrono::milliseconds kReconnectMaximum{ 30'000 };
    constexpr std::int32_t kMaxFrameBytes = 1024 * 1024;

    std::string ToUtf8(std::wstring const& text)
    {
        if (text.empty())
        {
            return {};
        }
        int required = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 1)
        {
            return {};
        }
        std::string out(static_cast<size_t>(required - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), required, nullptr, nullptr);
        return out;
    }

    std::string JsonEscape(std::wstring const& text)
    {
        auto utf8 = ToUtf8(text);
        std::string out;
        out.reserve(utf8.size() + 8);
        for (unsigned char ch : utf8)
        {
            switch (ch)
            {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (ch < 0x20)
                {
                    char buf[8];
                    sprintf_s(buf, "\\u%04x", ch);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(ch);
                }
                break;
            }
        }
        return out;
    }

    std::optional<std::string> JsonStringField(std::string const& json, char const* field)
    {
        std::string key = "\"" + std::string{ field } + "\"";
        auto keyPos = json.find(key);
        if (keyPos == std::string::npos)
        {
            return std::nullopt;
        }

        auto colon = json.find(':', keyPos + key.size());
        if (colon == std::string::npos)
        {
            return std::nullopt;
        }
        auto valuePos = json.find_first_not_of(" \t\r\n", colon + 1);
        if (valuePos == std::string::npos || json[valuePos] != '"')
        {
            return std::nullopt;
        }

        std::string value;
        bool escaped = false;
        for (size_t i = valuePos + 1; i < json.size(); ++i)
        {
            char ch = json[i];
            if (escaped)
            {
                value += ch;
                escaped = false;
            }
            else if (ch == '\\')
            {
                escaped = true;
            }
            else if (ch == '"')
            {
                return value;
            }
            else
            {
                value += ch;
            }
        }
        return std::nullopt;
    }

    bool IsReadyFrame(std::string const& payload)
    {
        auto command = JsonStringField(payload, "cmd");
        auto event = JsonStringField(payload, "evt");
        return command && event && *command == "DISPATCH" && *event == "READY";
    }

    bool IsErrorFrame(std::string const& payload)
    {
        auto event = JsonStringField(payload, "evt");
        return event && *event == "ERROR";
    }

    void TraceDiscord(char const* message)
    {
        OutputDebugStringA("Last Music Player Discord RPC: ");
        OutputDebugStringA(message);
        OutputDebugStringA("\n");
    }
}

namespace LastMusicPlayer::Backend
{
    DiscordPresence::~DiscordPresence()
    {
        Disconnect();
    }

    bool DiscordPresence::IsConnected() const
    {
        std::scoped_lock lock{ m_mutex };
        return m_ready;
    }

    void DiscordPresence::EnsureWorkerLocked()
    {
        if (!m_workerStarted)
        {
            m_workerStarted = true;
            m_ioThread = std::thread{ &DiscordPresence::IoMain, this };
        }
    }

    bool DiscordPresence::Connect()
    {
        bool ready = false;
        {
            std::scoped_lock lock{ m_mutex };
            if (m_stopping || kDiscordClientId[0] == '\0')
            {
                return false;
            }
            m_connectRequested = true;
            EnsureWorkerLocked();
            ready = m_ready;
        }
        m_wake.notify_all();
        return ready;
    }

    void DiscordPresence::Disconnect()
    {
        std::thread worker;
        {
            std::scoped_lock lock{ m_mutex };
            if (!m_workerStarted)
            {
                return;
            }
            m_stopping = true;
            m_connectRequested = false;
            m_clearRequested = false;
            worker = std::move(m_ioThread);
        }
        m_wake.notify_all();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    bool DiscordPresence::WriteFrame(int opcode, std::string const& payload)
    {
        if (m_pipe == INVALID_HANDLE_VALUE || payload.size() > static_cast<size_t>((std::numeric_limits<std::int32_t>::max)()))
        {
            return false;
        }

        std::vector<char> frame(8 + payload.size());
        std::int32_t op = opcode;
        std::int32_t len = static_cast<std::int32_t>(payload.size());
        std::memcpy(frame.data(), &op, sizeof(op));
        std::memcpy(frame.data() + 4, &len, sizeof(len));
        if (!payload.empty())
        {
            std::memcpy(frame.data() + 8, payload.data(), payload.size());
        }

        DWORD written = 0;
        if (!WriteFile(m_pipe, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr)
            || written != frame.size())
        {
            TraceDiscord("pipe write failed");
            return false;
        }
        return true;
    }

    DiscordPresence::FrameReadResult DiscordPresence::TryReadFrame(int& opcode, std::string& payload)
    {
        opcode = 0;
        payload.clear();
        if (m_pipe == INVALID_HANDLE_VALUE)
        {
            return FrameReadResult::Disconnected;
        }

        std::array<char, 8> header{};
        DWORD available = 0;
        if (!PeekNamedPipe(m_pipe, header.data(), static_cast<DWORD>(header.size()), nullptr, &available, nullptr))
        {
            return FrameReadResult::Disconnected;
        }
        if (available < header.size())
        {
            return FrameReadResult::NoFrame;
        }

        std::int32_t op = 0;
        std::int32_t length = 0;
        std::memcpy(&op, header.data(), sizeof(op));
        std::memcpy(&length, header.data() + 4, sizeof(length));
        if (length < 0 || length > kMaxFrameBytes || available < header.size() + static_cast<DWORD>(length))
        {
            if (length < 0 || length > kMaxFrameBytes)
            {
                TraceDiscord("received invalid frame length");
                return FrameReadResult::Disconnected;
            }
            return FrameReadResult::NoFrame;
        }

        DWORD read = 0;
        if (!ReadFile(m_pipe, header.data(), static_cast<DWORD>(header.size()), &read, nullptr) || read != header.size())
        {
            return FrameReadResult::Disconnected;
        }

        payload.resize(static_cast<size_t>(length));
        if (length > 0)
        {
            read = 0;
            if (!ReadFile(m_pipe, payload.data(), static_cast<DWORD>(payload.size()), &read, nullptr)
                || read != payload.size())
            {
                return FrameReadResult::Disconnected;
            }
        }
        opcode = op;
        return FrameReadResult::Frame;
    }

    void DiscordPresence::ClosePipe()
    {
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
    }

    bool DiscordPresence::OpenAndHandshake()
    {
        for (int i = 0; i < 10; ++i)
        {
            std::wstring path = std::wstring{ LMP_DISCORD_IPC_PIPE_PREFIX } + std::to_wstring(i);
            HANDLE pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe == INVALID_HANDLE_VALUE && GetLastError() == ERROR_PIPE_BUSY)
            {
                if (WaitNamedPipeW(path.c_str(), static_cast<DWORD>(kPipeBusyWait.count())))
                {
                    pipe = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        0, nullptr, OPEN_EXISTING, 0, nullptr);
                }
            }
            if (pipe == INVALID_HANDLE_VALUE)
            {
                continue;
            }

            m_pipe = pipe;
            std::string handshake =
                std::string("{\"v\":1,\"client_id\":\"") + kDiscordClientId + "\"}";
            if (!WriteFrame(0, handshake))
            {
                ClosePipe();
                continue;
            }

            auto deadline = std::chrono::steady_clock::now() + kHandshakeTimeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                {
                    std::scoped_lock lock{ m_mutex };
                    if (m_stopping)
                    {
                        ClosePipe();
                        return false;
                    }
                }

                int opcode = 0;
                std::string payload;
                auto result = TryReadFrame(opcode, payload);
                if (result == FrameReadResult::Disconnected)
                {
                    ClosePipe();
                    break;
                }
                if (result == FrameReadResult::Frame)
                {
                    if (opcode == 1 && IsReadyFrame(payload))
                    {
                        return true;
                    }
                    if (opcode == 3 && !WriteFrame(4, payload))
                    {
                        ClosePipe();
                        break;
                    }
                    if (opcode == 2)
                    {
                        ClosePipe();
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
            ClosePipe();
        }

        TraceDiscord("Discord IPC handshake was not accepted");
        return false;
    }

    std::string DiscordPresence::NextNonce(uint64_t sequence)
    {
        return std::to_string(GetTickCount64()) + "-" + std::to_string(sequence);
    }

    std::string DiscordPresence::BuildActivityJson(PresencePayload const& p, std::string const& nonce) const
    {
        auto now = std::chrono::system_clock::now();
        long long nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        double position = (std::max)(0.0, p.positionSeconds);
        double duration = (std::max)(0.0, p.durationSeconds);
        long long startMs = nowMs - static_cast<long long>(position * 1000.0);
        long long endMs = nowMs + static_cast<long long>((std::max)(0.0, duration - position) * 1000.0);

        std::ostringstream os;
        os << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"
           << GetCurrentProcessId()
           << ",\"activity\":{\"type\":2,\"name\":\"Last Music\"";

        if (!p.title.empty())
        {
            os << ",\"details\":\"" << JsonEscape(p.title) << "\"";
        }

        std::wstring state = p.artist;
        if (!p.isPlaying)
        {
            if (!state.empty()) state += L" · ";
            state += L"Paused";
        }
        if (!state.empty())
        {
            os << ",\"state\":\"" << JsonEscape(state) << "\"";
        }

        if (p.isPlaying && duration > 0.5)
        {
            os << ",\"timestamps\":{\"start\":" << startMs
               << ",\"end\":" << endMs << "}";
        }

        bool isHttpsUrl = p.artworkUrl.rfind(L"https://", 0) == 0;
        bool isDiscordExternalAsset = p.artworkUrl.rfind(L"mp:external/", 0) == 0;
        bool isBareAssetKey = !p.artworkUrl.empty()
            && p.artworkUrl.find(L'/') == std::wstring::npos;
        if (isHttpsUrl || isDiscordExternalAsset || isBareAssetKey)
        {
            os << ",\"assets\":{\"large_image\":\"" << JsonEscape(p.artworkUrl) << "\"";
            std::wstring sourceLabel = p.isLocal ? L"Source: Local" : L"Source: Remote";
            os << ",\"large_text\":\"" << JsonEscape(sourceLabel) << "\"}";
        }

        os << "}},\"nonce\":\"" << nonce << "\"}";
        return os.str();
    }

    std::string DiscordPresence::BuildActivityFingerprint(PresencePayload const& p)
    {
        auto append = [](std::ostringstream& out, std::wstring const& value) {
            auto utf8 = ToUtf8(value);
            out << utf8.size() << ':' << utf8 << '|';
        };
        auto milliseconds = [](double seconds) {
            return static_cast<long long>(std::llround((std::max)(0.0, seconds) * 1000.0));
        };

        std::ostringstream os;
        append(os, p.title);
        append(os, p.artist);
        append(os, p.artworkUrl);
        os << milliseconds(p.durationSeconds) << '|'
           << milliseconds(p.positionSeconds) << '|'
           << p.isPlaying << '|' << p.isLocal;
        return os.str();
    }

    std::string DiscordPresence::BuildClearJson(std::string const& nonce)
    {
        return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"
            + std::to_string(GetCurrentProcessId())
            + ",\"activity\":null},\"nonce\":\"" + nonce + "\"}";
    }

    void DiscordPresence::HandleIncomingFrame(int opcode, std::string const& payload)
    {
        if (opcode != 1)
        {
            return;
        }

        auto nonce = JsonStringField(payload, "nonce");
        if (!nonce)
        {
            return;
        }

        bool wasError = IsErrorFrame(payload);
        std::scoped_lock lock{ m_mutex };
        if (!m_waitingForReply || *nonce != m_waitingNonce)
        {
            return;
        }

        if (wasError)
        {
            // The payload was accepted by the IPC server but rejected by
            // Discord. Treat it as acknowledged so an invalid asset or
            // unsupported client does not create an infinite resend loop;
            // the next real playback change still produces a fresh payload.
            TraceDiscord("Discord rejected a SET_ACTIVITY frame");
        }
        m_lastAcknowledgedFingerprint = m_waitingFingerprint;
        bool clearComplete = m_waitingForClear;
        m_waitingForReply = false;
        m_waitingForClear = false;
        m_waitingNonce.clear();
        m_waitingFingerprint.clear();
        if (clearComplete)
        {
            // A quick off/on toggle may have queued a new track while this
            // clear was in flight. Only retire the connection when clear is
            // still the latest desired state.
            if (m_clearRequested)
            {
                m_clearRequested = false;
                m_connectRequested = false;
            }
        }
    }

    bool DiscordPresence::DrainIncomingFrames()
    {
        for (int i = 0; i < 32; ++i)
        {
            int opcode = 0;
            std::string payload;
            auto result = TryReadFrame(opcode, payload);
            if (result == FrameReadResult::NoFrame)
            {
                return true;
            }
            if (result == FrameReadResult::Disconnected)
            {
                return false;
            }
            if (opcode == 2)
            {
                TraceDiscord("Discord closed the IPC pipe");
                return false;
            }
            if (opcode == 3)
            {
                if (!WriteFrame(4, payload))
                {
                    return false;
                }
                continue;
            }
            HandleIncomingFrame(opcode, payload);
        }
        return true;
    }

    void DiscordPresence::MarkDisconnected()
    {
        ClosePipe();
        std::scoped_lock lock{ m_mutex };
        m_ready = false;
        m_waitingForReply = false;
        m_waitingForClear = false;
        m_waitingNonce.clear();
        m_waitingFingerprint.clear();
        // Discord may have lost the activity even if it acknowledged an older
        // update, so a successful reconnect must always re-send the latest one.
        m_lastAcknowledgedFingerprint.clear();
        if (m_clearRequested)
        {
            m_clearRequested = false;
        }
    }

    void DiscordPresence::ScheduleReconnect()
    {
        std::scoped_lock lock{ m_mutex };
        if (m_stopping || !m_connectRequested || !m_last)
        {
            return;
        }
        auto jitter = std::chrono::milliseconds(GetTickCount64() % 251);
        m_nextReconnectAt = std::chrono::steady_clock::now() + m_reconnectDelay + jitter;
        m_reconnectDelay = (std::min)(m_reconnectDelay * 2, kReconnectMaximum);
    }

    void DiscordPresence::IoMain()
    {
        for (;;)
        {
            bool ready = false;
            bool shouldConnect = false;
            std::chrono::steady_clock::time_point reconnectAt;
            {
                std::scoped_lock lock{ m_mutex };
                if (m_stopping)
                {
                    break;
                }
                ready = m_ready;
                shouldConnect = m_connectRequested && m_last.has_value();
                reconnectAt = m_nextReconnectAt;
            }

            if (!ready)
            {
                auto now = std::chrono::steady_clock::now();
                if (shouldConnect && (reconnectAt.time_since_epoch().count() == 0 || now >= reconnectAt))
                {
                    if (OpenAndHandshake())
                    {
                        std::scoped_lock lock{ m_mutex };
                        if (!m_stopping)
                        {
                            m_ready = true;
                            m_reconnectDelay = std::chrono::milliseconds(1000);
                            m_nextReconnectAt = {};
                            m_lastAcknowledgedFingerprint.clear();
                        }
                        else
                        {
                            ClosePipe();
                        }
                    }
                    else
                    {
                        ScheduleReconnect();
                    }
                    continue;
                }

                std::unique_lock lock{ m_mutex };
                if (m_stopping)
                {
                    break;
                }
                if (shouldConnect && reconnectAt.time_since_epoch().count() != 0)
                {
                    m_wake.wait_until(lock, reconnectAt);
                }
                else
                {
                    m_wake.wait(lock, [this] {
                        return m_stopping || (m_connectRequested && m_last.has_value());
                    });
                }
                continue;
            }

            if (!DrainIncomingFrames())
            {
                MarkDisconnected();
                ScheduleReconnect();
                continue;
            }

            std::optional<PresencePayload> activity;
            bool clear = false;
            bool waiting = false;
            std::chrono::steady_clock::time_point replyDeadline;
            std::string acknowledgedFingerprint;
            {
                std::scoped_lock lock{ m_mutex };
                if (m_stopping)
                {
                    break;
                }
                clear = m_clearRequested;
                activity = m_last;
                waiting = m_waitingForReply;
                replyDeadline = m_replyDeadline;
                acknowledgedFingerprint = m_lastAcknowledgedFingerprint;
                if (!m_connectRequested && !clear && !waiting)
                {
                    m_ready = false;
                    ClosePipe();
                    continue;
                }
            }

            auto now = std::chrono::steady_clock::now();
            if (waiting)
            {
                if (now >= replyDeadline)
                {
                    TraceDiscord("timed out waiting for Discord RPC reply");
                    MarkDisconnected();
                    ScheduleReconnect();
                    continue;
                }
            }
            else
            {
                std::string fingerprint;
                std::string json;
                bool isClear = false;
                if (clear)
                {
                    fingerprint = "__clear__";
                    isClear = true;
                }
                else if (activity)
                {
                    fingerprint = BuildActivityFingerprint(*activity);
                }

                bool hasCommand = !fingerprint.empty();
                bool due = m_lastSendAt.time_since_epoch().count() == 0
                    || now - m_lastSendAt >= kMinSendGap;
                if (hasCommand && fingerprint != acknowledgedFingerprint && (isClear || due))
                {
                    std::string nonce;
                    {
                        std::scoped_lock lock{ m_mutex };
                        nonce = NextNonce(++m_nextNonce);
                    }
                    json = isClear ? BuildClearJson(nonce) : BuildActivityJson(*activity, nonce);
                    if (!WriteFrame(1, json))
                    {
                        MarkDisconnected();
                        ScheduleReconnect();
                        continue;
                    }

                    std::scoped_lock lock{ m_mutex };
                    m_lastSendAt = std::chrono::steady_clock::now();
                    m_waitingForReply = true;
                    m_waitingForClear = isClear;
                    m_waitingNonce = std::move(nonce);
                    m_waitingFingerprint = std::move(fingerprint);
                    m_replyDeadline = m_lastSendAt + kReplyTimeout;
                }
            }

            std::unique_lock lock{ m_mutex };
            if (!m_stopping)
            {
                auto wakeAt = m_waitingForReply
                    ? m_replyDeadline
                    : (m_lastSendAt.time_since_epoch().count() == 0
                        ? std::chrono::steady_clock::now() + kIdlePoll
                        : m_lastSendAt + kMinSendGap);
                m_wake.wait_until(lock, wakeAt);
            }
        }

        ClosePipe();
        std::scoped_lock lock{ m_mutex };
        m_ready = false;
        m_waitingForReply = false;
    }

    void DiscordPresence::SetNowPlaying(PresencePayload const& payload)
    {
        if (payload.title.empty())
        {
            Clear();
            return;
        }

        {
            std::scoped_lock lock{ m_mutex };
            if (m_stopping || kDiscordClientId[0] == '\0')
            {
                return;
            }
            m_last = payload;
            m_clearRequested = false;
            m_connectRequested = true;
            EnsureWorkerLocked();
        }
        m_wake.notify_all();
    }

    void DiscordPresence::SetPlaybackState(bool isPlaying, double positionSeconds, double durationSeconds)
    {
        {
            std::scoped_lock lock{ m_mutex };
            if (!m_last || m_stopping) return;
            m_last->isPlaying = isPlaying;
            m_last->positionSeconds = positionSeconds;
            if (durationSeconds > 0.5) m_last->durationSeconds = durationSeconds;
        }
        m_wake.notify_all();
    }

    void DiscordPresence::SetDuration(double durationSeconds)
    {
        if (durationSeconds <= 0.5) return;
        {
            std::scoped_lock lock{ m_mutex };
            if (!m_last || m_stopping) return;
            if (std::abs(m_last->durationSeconds - durationSeconds) < 1.0) return;
            m_last->durationSeconds = durationSeconds;
        }
        m_wake.notify_all();
    }

    void DiscordPresence::SetArtworkProxyUrl(
        std::wstring const& proxyUrl,
        std::wstring const& originalTitle,
        std::wstring const& originalArtist)
    {
        if (proxyUrl.empty())
        {
            return;
        }
        {
            std::scoped_lock lock{ m_mutex };
            if (!m_last || m_stopping
                || m_last->title != originalTitle
                || m_last->artist != originalArtist)
            {
                return;
            }
            m_last->artworkUrl = proxyUrl;
        }
        m_wake.notify_all();
    }

    void DiscordPresence::SetPosition(double positionSeconds)
    {
        {
            std::scoped_lock lock{ m_mutex };
            if (!m_last || m_stopping) return;
            m_last->positionSeconds = positionSeconds;
        }
        m_wake.notify_all();
    }

    void DiscordPresence::Clear()
    {
        {
            std::scoped_lock lock{ m_mutex };
            if (m_stopping) return;
            m_last.reset();
            // If a validated connection exists, clear it before the worker
            // closes the pipe. If Discord is already gone there is no local
            // activity to clean up, so do not revive the IPC client just to
            // issue a stale clear.
            m_clearRequested = m_ready;
            m_connectRequested = false;
            m_lastAcknowledgedFingerprint.clear();
        }
        m_wake.notify_all();
    }
}
