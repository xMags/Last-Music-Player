#include "pch.h"

#include <winrt/Windows.Foundation.h>

#include "Backend/ProviderHelpers.h"
#include "Backend/DiscordPresence.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace provider = LastMusicPlayer::Backend;
namespace discord = LastMusicPlayer::Backend;

namespace
{
    std::wstring ToWide(winrt::hstring const& value)
    {
        return std::wstring{ value.c_str() };
    }

    bool Contains(winrt::hstring const& value, wchar_t const* expected)
    {
        return ToWide(value).find(expected) != std::wstring::npos;
    }

    bool StartsWith(winrt::hstring const& value, wchar_t const* expected)
    {
        return ToWide(value).rfind(expected, 0) == 0;
    }

    std::wstring MediaToken(wchar_t const* scope, long long offsetMs)
    {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return std::wstring{ scope } + L"." + std::to_wstring(nowMs + offsetMs) + L".signature";
    }

    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestNormalizeBaseUrl()
    {
        Expect(provider::NormalizeProviderBaseUrl(L"") == L"http://127.0.0.1:4527", "empty base URL should use the local default");
        Expect(provider::NormalizeProviderBaseUrl(L"https://music.example.test///") == L"https://music.example.test", "base URL should trim trailing slashes");
        Expect(provider::NormalizeProviderBaseUrl(L"https://music.example.test\\") == L"https://music.example.test", "base URL should trim trailing backslashes");
    }

    void TestExistingStreamUrlIsStable()
    {
        auto existing = winrt::hstring{ L"https://provider.example.test/v1/stream/direct%3A1?url=https%3A%2F%2Faudio.example.test%2Fa.mp3&media_token=short" };
        auto stream = provider::BuildProviderStreamUrl(existing, L"", L"", L"", L"http://127.0.0.1:4527", L"");
        Expect(!Contains(stream, L"media_token="), "existing provider stream URLs should drop stale media tokens");
        Expect(Contains(stream, L"?url=https%3A%2F%2Faudio.example.test%2Fa.mp3"), "existing provider stream URLs should keep the source URL");

        auto legacy = winrt::hstring{ L"https://provider.example.test/v1/stream/direct%3A1?access_token=full-key&url=https%3A%2F%2Faudio.example.test%2Fa.mp3&media_token=short" };
        auto sanitized = provider::BuildProviderStreamUrl(legacy, L"", L"", L"", L"http://127.0.0.1:4527", L"");
        Expect(!Contains(sanitized, L"access_token="), "existing media-token stream URLs should drop legacy access tokens");
        Expect(!Contains(sanitized, L"media_token="), "existing media-token stream URLs should drop stale media tokens");
    }

    void TestProviderStreamUrl()
    {
        auto stream = provider::BuildProviderStreamUrl(
            L"",
            L"https://catalog.example.test/tracks/abc 123",
            L"internal-provider",
            L"",
            L"https://provider.example.test/",
            L"key value");

        Expect(StartsWith(stream, L"https://provider.example.test/v1/stream/remote%3A"), "remote tracks should use a generic stream id");
        Expect(Contains(stream, L"?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc%20123"), "source URL should be escaped into the stream query");
        Expect(Contains(stream, L"access_token=key%20value"), "provider stream URLs should include the escaped API token");
    }

    void TestReturnedStreamUrlWins()
    {
        auto stream = provider::BuildProviderStreamUrl(
            L"http://old-provider.example.test/v1/stream/service%3Aissued-id?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc&media_token=short",
            L"https://catalog.example.test/tracks/abc",
            L"service",
            L"",
            L"https://provider.example.test/",
            L"fresh key");

        Expect(StartsWith(stream, L"https://provider.example.test/v1/stream/service%3Aissued-id"), "returned stream ids should be preserved");
        Expect(Contains(stream, L"?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc"), "returned stream URLs should keep their source query");
        Expect(!Contains(stream, L"media_token="), "returned stream URLs should drop stale media tokens");
        Expect(Contains(stream, L"access_token=fresh%20key"), "returned stream URLs should get a fresh access token");
    }

    void TestPersistedImportedStreamUrl()
    {
        auto stream = provider::BuildProviderStreamUrl(
            L"http://old-provider.example.test/v1/stream/service%3Aissued-id?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc&media_token=short#lmp=12345",
            L"https://catalog.example.test/tracks/abc",
            L"service",
            L"",
            L"https://provider.example.test/",
            L"fresh key");

        Expect(StartsWith(stream, L"https://provider.example.test/v1/stream/service%3Aissued-id"), "persisted stream ids should be preserved");
        Expect(Contains(stream, L"?url=https%3A%2F%2Fcatalog.example.test%2Ftracks%2Fabc"), "persisted stream URLs should keep their source query");
        Expect(!Contains(stream, L"media_token="), "persisted stream URLs should drop stale media tokens");
        Expect(Contains(stream, L"access_token=fresh%20key"), "persisted stream URLs should send the fresh access token");
        Expect(!Contains(stream, L"#lmp="), "internal storage fragments should not reach playback");
    }

    void TestDirectStreamUrl()
    {
        auto stream = provider::BuildProviderStreamUrl(
            L"",
            L"https://cdn.example.test/audio/song.mp3",
            L"",
            L"",
            L"http://127.0.0.1:4527/",
            L"");

        Expect(StartsWith(stream, L"http://127.0.0.1:4527/v1/stream/direct%3A"), "empty provider with an HTTP source should use the direct stream provider");
        Expect(Contains(stream, L"?url=https%3A%2F%2Fcdn.example.test%2Faudio%2Fsong.mp3"), "direct source URL should be escaped into the stream query");
        Expect(!Contains(stream, L"access_token="), "direct stream URL should omit access token when no token is available");
    }

    void TestFallbackTokenAndUnsupportedSources()
    {
        auto fallback = provider::BuildProviderStreamUrl(
            L"https://provider.example.test/v1/track/1?access_token=legacy%20key",
            L"https://cdn.example.test/audio/song.mp3",
            L"direct",
            L"",
            L"http://127.0.0.1:4527",
            L"");

        Expect(!Contains(fallback, L"access_token="), "legacy access token from file path should not be reused in new fallback stream URLs");
        Expect(provider::BuildProviderStreamUrl(L"", L"custom-scheme://track/1", L"unsupported", L"", L"", L"").empty(), "unsupported sources should not build stream URLs");
        Expect(provider::BuildProviderStreamUrl(L"", L"file:///C:/Music/a.mp3", L"direct", L"", L"", L"").empty(), "non-HTTP sources should not build stream URLs");
    }

    void TestSignedStreamUrl()
    {
        auto token = MediaToken(L"stream", 5 * 60 * 1000);
        auto existing = winrt::hstring{
            L"https://provider.example.test/v1/stream/direct%3A1?s=opaque&access_token=legacy-secret&media_token="
            + token
            + L"#lmp=123" };
        auto stream = provider::BuildProviderStreamUrl(existing, L"https://provider.example.test");

        Expect(!stream.empty(), "current provider stream URL should be accepted");
        Expect(Contains(stream, L"s=opaque"), "opaque source token should be preserved");
        Expect(Contains(stream, L"media_token=stream."), "signed media token should be preserved");
        Expect(!Contains(stream, L"access_token="), "legacy query credential should be removed");
        Expect(!Contains(stream, L"#lmp="), "database-only fragment should not reach the media component");

        Expect(provider::BuildProviderStreamUrl(existing, L"https://other.example.test").empty(),
            "stream URL from a different provider host should be refreshed");
        Expect(provider::BuildProviderStreamUrl(
            L"https://cdn.example.test/song.mp3", L"https://provider.example.test").empty(),
            "raw source URL should not bypass the provider contract");
    }

    void TestMediaTokenRefresh()
    {
        auto future = winrt::hstring{
            L"https://provider.example.test/v1/stream/direct%3A1?media_token="
            + MediaToken(L"stream", 5 * 60 * 1000) };
        auto expired = winrt::hstring{
            L"https://provider.example.test/v1/stream/direct%3A1?media_token="
            + MediaToken(L"stream", -60 * 1000) };
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        auto emptySignature = winrt::hstring{
            L"https://provider.example.test/v1/stream/direct%3A1?media_token=stream."
            + std::to_wstring(nowMs + 5 * 60 * 1000)
            + L"." };

        Expect(!provider::ProviderMediaUrlNeedsRefresh(future, L"stream", true),
            "fresh signed stream should not refresh");
        Expect(provider::ProviderMediaUrlNeedsRefresh(expired, L"stream", true),
            "expired signed stream should refresh");
        Expect(provider::ProviderMediaUrlNeedsRefresh(
            L"https://provider.example.test/v1/stream/direct%3A1", L"stream", true),
            "missing signed token should refresh");
        Expect(provider::ProviderMediaUrlNeedsRefresh(
            L"https://provider.example.test/v1/stream/direct%3A1?media_token=broken", L"stream", true),
            "malformed signed token should refresh");
        Expect(provider::ProviderMediaUrlNeedsRefresh(emptySignature, L"stream", true),
            "signed token with an empty signature should refresh");
        Expect(provider::ProviderMediaUrlNeedsRefresh(future, L"unsupported", true),
            "unsupported signed token scope should refresh");
        Expect(!provider::ProviderMediaUrlNeedsRefresh(
            L"https://provider.example.test/v1/stream/direct%3A1", L"stream", false),
            "open local provider should not require a token");
        Expect(!provider::ProviderMediaUrlNeedsRefresh(
            L"https://images.example.test/cover.jpg", L"artwork", true),
            "external artwork should not use provider token rules");
    }

    void TestSignedArtworkUrl()
    {
        auto external = winrt::hstring{ L"https://images.example.test/cover.jpg" };
        Expect(provider::BuildProviderArtworkUrl(external, L"https://provider.example.test") == external,
            "external artwork URL should pass through unchanged");

        auto signedArtwork = winrt::hstring{
            L"https://provider.example.test/v1/artwork?url=opaque&access_token=legacy-secret&media_token="
            + MediaToken(L"artwork", 5 * 60 * 1000) };
        auto safeArtwork = provider::BuildProviderArtworkUrl(
            signedArtwork, L"https://provider.example.test");
        Expect(!safeArtwork.empty(), "current signed artwork URL should be accepted");
        Expect(Contains(safeArtwork, L"media_token=artwork."), "artwork token should be preserved");
        Expect(!Contains(safeArtwork, L"access_token="), "artwork URL should not contain a query credential");
        Expect(provider::BuildProviderArtworkUrl(
            signedArtwork, L"https://other.example.test").empty(),
            "artwork URL from a different provider host should be refreshed");
    }

    void TestRemoteUrlSafety()
    {
        Expect(provider::IsSafeRemoteUrl(
            L"https://catalog.example.test/watch?id=stable", provider::RemoteUrlUse::Durable),
            "stable HTTPS URL should be durable");
        Expect(provider::IsSafeRemoteUrl(
            L"https://catalog.example.test/watch?monkey=value", provider::RemoteUrlUse::Durable),
            "benign parameter names containing a credential suffix should remain durable");
        Expect(provider::IsSafeRemoteUrl(
            L"http://127.0.0.1:4527/watch?id=stable", provider::RemoteUrlUse::Durable),
            "loopback HTTP URL should be durable");
        Expect(!provider::IsSafeRemoteUrl(
            L"http://catalog.example.test/watch?id=stable", provider::RemoteUrlUse::Durable),
            "non-loopback HTTP URL should be rejected");
        Expect(!provider::IsSafeRemoteUrl(
            L"https://user:secret@catalog.example.test/watch?id=stable", provider::RemoteUrlUse::EphemeralMedia),
            "URL user information should be rejected");
        Expect(!provider::IsSafeRemoteUrl(
            L"https://catalog.example.test/watch?access_token=secret", provider::RemoteUrlUse::EphemeralMedia),
            "long-lived credential query should be rejected");
        Expect(provider::IsSafeRemoteUrl(
            L"https://cdn.example.test/song?media_token=stream.1.signature", provider::RemoteUrlUse::EphemeralMedia),
            "signed media URL should be allowed ephemerally");
        Expect(!provider::IsSafeRemoteUrl(
            L"https://cdn.example.test/song?media_token=stream.1.signature", provider::RemoteUrlUse::Durable),
            "signed media URL should not be durable");
        Expect(!provider::IsSafeRemoteUrl(
            L"https://catalog.example.test/watch#session=secret", provider::RemoteUrlUse::Durable),
            "credential-bearing fragment should not be durable");
    }

    constexpr wchar_t kDiscordTestPipe[] = L"\\\\.\\pipe\\lmp-discord-test-0";

    struct RpcFrame
    {
        int Opcode{};
        std::string Payload;
    };

    bool ReadExactly(HANDLE pipe, void* buffer, DWORD size)
    {
        auto* cursor = static_cast<unsigned char*>(buffer);
        while (size > 0)
        {
            DWORD read = 0;
            if (!ReadFile(pipe, cursor, size, &read, nullptr) || read == 0)
            {
                return false;
            }
            cursor += read;
            size -= read;
        }
        return true;
    }

    bool WriteExactly(HANDLE pipe, void const* buffer, DWORD size)
    {
        auto const* cursor = static_cast<unsigned char const*>(buffer);
        while (size > 0)
        {
            DWORD written = 0;
            if (!WriteFile(pipe, cursor, size, &written, nullptr) || written == 0)
            {
                return false;
            }
            cursor += written;
            size -= written;
        }
        return true;
    }

    RpcFrame ReadRpcFrame(HANDLE pipe)
    {
        std::int32_t header[2]{};
        Expect(ReadExactly(pipe, header, sizeof(header)), "test Discord server could not read RPC header");
        Expect(header[1] >= 0 && header[1] <= 1024 * 1024, "test Discord server received invalid RPC length");
        RpcFrame frame;
        frame.Opcode = header[0];
        frame.Payload.resize(static_cast<size_t>(header[1]));
        if (!frame.Payload.empty())
        {
            Expect(ReadExactly(pipe, frame.Payload.data(), static_cast<DWORD>(frame.Payload.size())), "test Discord server could not read RPC payload");
        }
        return frame;
    }

    void WriteRpcFrame(HANDLE pipe, int opcode, std::string const& payload)
    {
        std::int32_t header[2]{ opcode, static_cast<std::int32_t>(payload.size()) };
        Expect(WriteExactly(pipe, header, sizeof(header)), "test Discord server could not write RPC header");
        if (!payload.empty())
        {
            Expect(WriteExactly(pipe, payload.data(), static_cast<DWORD>(payload.size())), "test Discord server could not write RPC payload");
        }
    }

    std::string JsonField(std::string const& json, char const* field)
    {
        std::string key = "\"" + std::string{ field } + "\"";
        auto keyPos = json.find(key);
        if (keyPos == std::string::npos) return {};
        auto colon = json.find(':', keyPos + key.size());
        if (colon == std::string::npos) return {};
        auto value = json.find('"', colon + 1);
        if (value == std::string::npos) return {};
        auto end = json.find('"', value + 1);
        return end == std::string::npos ? std::string{} : json.substr(value + 1, end - value - 1);
    }

    bool WaitUntil(std::function<bool()> const& condition, std::chrono::milliseconds timeout = std::chrono::seconds(3))
    {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition()) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return condition();
    }

    class FakeDiscordServer
    {
    public:
        explicit FakeDiscordServer(bool acknowledgeActivity, bool expectClear)
            : m_thread([this, acknowledgeActivity, expectClear] { Run(acknowledgeActivity, expectClear); })
        {
            Expect(WaitUntil([this] { return m_created.load(); }), "test Discord pipe was not created");
            if (m_failed.load())
            {
                if (m_thread.joinable()) m_thread.join();
                std::rethrow_exception(m_error);
            }
        }

        ~FakeDiscordServer()
        {
            if (m_thread.joinable()) m_thread.join();
        }

        bool ActivityReceived() const { return m_activityReceived.load(); }
        bool ClearReceived() const { return m_clearReceived.load(); }

        void Join()
        {
            if (m_thread.joinable()) m_thread.join();
            if (m_error) std::rethrow_exception(m_error);
        }

    private:
        void Run(bool acknowledgeActivity, bool expectClear)
        {
            HANDLE pipe = INVALID_HANDLE_VALUE;
            try
            {
                pipe = CreateNamedPipeW(
                    kDiscordTestPipe,
                    PIPE_ACCESS_DUPLEX,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                    1,
                    4096,
                    4096,
                    0,
                    nullptr);
                Expect(pipe != INVALID_HANDLE_VALUE, "test Discord server could not create named pipe");
                m_created = true;

                BOOL connected = ConnectNamedPipe(pipe, nullptr);
                Expect(connected || GetLastError() == ERROR_PIPE_CONNECTED, "test Discord server could not accept client");

                auto handshake = ReadRpcFrame(pipe);
                Expect(handshake.Opcode == 0, "client did not start with Discord handshake");
                Expect(handshake.Payload.find("\"client_id\":\"lmp-native-test\"") != std::string::npos, "client used the wrong test application id");
                WriteRpcFrame(pipe, 1, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{}}");

                auto activity = ReadRpcFrame(pipe);
                Expect(activity.Opcode == 1, "client did not send SET_ACTIVITY after READY");
                Expect(activity.Payload.find("\"cmd\":\"SET_ACTIVITY\"") != std::string::npos, "client sent the wrong Discord command");
                Expect(activity.Payload.find("\"details\":\"Native test track\"") != std::string::npos, "activity payload lost track details");
                auto activityNonce = JsonField(activity.Payload, "nonce");
                Expect(!activityNonce.empty(), "activity payload omitted nonce");
                m_activityReceived = true;

                if (!acknowledgeActivity)
                {
                    CloseHandle(pipe);
                    return;
                }
                WriteRpcFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"nonce\":\"" + activityNonce + "\"}");

                if (expectClear)
                {
                    auto clear = ReadRpcFrame(pipe);
                    Expect(clear.Opcode == 1 && clear.Payload.find("\"activity\":null") != std::string::npos, "client did not send a clear activity frame");
                    auto clearNonce = JsonField(clear.Payload, "nonce");
                    Expect(!clearNonce.empty(), "clear payload omitted nonce");
                    m_clearReceived = true;
                    WriteRpcFrame(pipe, 1, "{\"cmd\":\"SET_ACTIVITY\",\"data\":{},\"nonce\":\"" + clearNonce + "\"}");
                }
                CloseHandle(pipe);
            }
            catch (...)
            {
                if (pipe != INVALID_HANDLE_VALUE) CloseHandle(pipe);
                m_error = std::current_exception();
                m_failed = true;
                m_created = true;
            }
        }

        std::thread m_thread;
        std::atomic<bool> m_created{ false };
        std::atomic<bool> m_activityReceived{ false };
        std::atomic<bool> m_clearReceived{ false };
        std::atomic<bool> m_failed{ false };
        std::exception_ptr m_error;
    };

    void TestDiscordReadyAckAndClear()
    {
        FakeDiscordServer server{ true, true };
        discord::DiscordPresence presence;
        discord::PresencePayload payload;
        payload.title = L"Native test track";
        payload.artist = L"Native test artist";
        payload.durationSeconds = 240.0;
        payload.positionSeconds = 12.0;
        presence.SetNowPlaying(payload);

        Expect(WaitUntil([&] { return presence.IsConnected(); }), "presence never became READY");
        Expect(WaitUntil([&] { return server.ActivityReceived(); }), "server never received the activity frame");
        presence.Clear();
        Expect(WaitUntil([&] { return server.ClearReceived(); }), "server never received the clear frame");
        presence.Disconnect();
        server.Join();
    }

    void TestDiscordDisconnectWithOutstandingReply()
    {
        FakeDiscordServer server{ false, false };
        discord::DiscordPresence presence;
        discord::PresencePayload payload;
        payload.title = L"Native test track";
        payload.artist = L"Native test artist";
        presence.SetNowPlaying(payload);
        Expect(WaitUntil([&] { return server.ActivityReceived(); }), "server never received the unacknowledged activity frame");

        auto started = std::chrono::steady_clock::now();
        presence.Disconnect();
        auto elapsed = std::chrono::steady_clock::now() - started;
        Expect(elapsed < std::chrono::seconds(1), "disconnect blocked while an RPC reply was outstanding");
        server.Join();
    }
}

int wmain()
{
    try
    {
        winrt::init_apartment();
        TestNormalizeBaseUrl();
        TestExistingStreamUrlIsStable();
        TestProviderStreamUrl();
        TestReturnedStreamUrlWins();
        TestPersistedImportedStreamUrl();
        TestDirectStreamUrl();
        TestFallbackTokenAndUnsupportedSources();
        TestSignedStreamUrl();
        TestMediaTokenRefresh();
        TestSignedArtworkUrl();
        TestRemoteUrlSafety();
        TestDiscordReadyAckAndClear();
        TestDiscordDisconnectWithOutstandingReply();
        std::wcout << L"ProviderHelpersTests passed" << std::endl;
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "ProviderHelpersTests failed: " << error.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "ProviderHelpersTests failed with an unknown exception" << std::endl;
        return 1;
    }
}
