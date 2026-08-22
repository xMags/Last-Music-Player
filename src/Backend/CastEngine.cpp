#include "pch.h"
#include "Backend/CastEngine.h"

#include <windows.h>
#include <windns.h>

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Security.Cryptography.Certificates.h>

#include <chrono>
#include <set>

#pragma comment(lib, "dnsapi.lib")

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Networking;
using namespace winrt::Windows::Networking::Sockets;
using namespace winrt::Windows::Storage::Streams;
namespace WDJ = winrt::Windows::Data::Json;

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr char kNsConnection[] = "urn:x-cast:com.google.cast.tp.connection";
        constexpr char kNsHeartbeat[] = "urn:x-cast:com.google.cast.tp.heartbeat";
        constexpr char kNsReceiver[] = "urn:x-cast:com.google.cast.receiver";
        constexpr char kNsMedia[] = "urn:x-cast:com.google.cast.media";
        constexpr char kSourceId[] = "sender-0";
        constexpr char kDefaultReceiver[] = "CC1AD845";

        void WriteVarint(std::vector<uint8_t>& out, uint64_t value)
        {
            while (value >= 0x80)
            {
                out.push_back(static_cast<uint8_t>(value) | 0x80);
                value >>= 7;
            }
            out.push_back(static_cast<uint8_t>(value));
        }

        void WriteLenField(std::vector<uint8_t>& out, uint8_t fieldNo, std::string const& data)
        {
            out.push_back(static_cast<uint8_t>((fieldNo << 3) | 2)); // wire type 2
            WriteVarint(out, data.size());
            out.insert(out.end(), data.begin(), data.end());
        }

        // CastMessage: 1 protocol_version(varint,0) 2 source 3 dest 4 namespace
        // 5 payload_type(varint,0=STRING) 6 payload_utf8.
        std::vector<uint8_t> EncodeCastMessage(
            std::string const& ns,
            std::string const& destination,
            std::string const& payload)
        {
            std::vector<uint8_t> body;
            body.push_back((1 << 3) | 0);
            body.push_back(0); // protocol_version = 0
            WriteLenField(body, 2, kSourceId);
            WriteLenField(body, 3, destination);
            WriteLenField(body, 4, ns);
            body.push_back((5 << 3) | 0);
            body.push_back(0); // payload_type = STRING
            WriteLenField(body, 6, payload);

            std::vector<uint8_t> frame;
            frame.reserve(body.size() + 4);
            uint32_t len = static_cast<uint32_t>(body.size());
            frame.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
            frame.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
            frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
            frame.push_back(static_cast<uint8_t>(len & 0xFF));
            frame.insert(frame.end(), body.begin(), body.end());
            return frame;
        }

        uint64_t ReadVarint(uint8_t const* data, size_t size, size_t& pos)
        {
            uint64_t result = 0;
            int shift = 0;
            // Bound shift to <=63. A valid uint64 varint is at most 10
            // bytes; without this guard a malformed input that keeps the
            // continuation bit set drives shift past 63 and triggers UB
            // on the left-shift. Callers tolerate a 0 return as "no tag".
            while (pos < size && shift <= 63)
            {
                uint8_t b = data[pos++];
                result |= static_cast<uint64_t>(b & 0x7F) << shift;
                if (!(b & 0x80)) break;
                shift += 7;
            }
            return result;
        }

        // Extract namespace (field 4) and payload_utf8 (field 6).
        void DecodeCastMessage(
            uint8_t const* data,
            size_t size,
            std::string& nsOut,
            std::string& payloadOut)
        {
            size_t pos = 0;
            while (pos < size)
            {
                uint64_t tag = ReadVarint(data, size, pos);
                uint32_t field = static_cast<uint32_t>(tag >> 3);
                uint32_t wtype = static_cast<uint32_t>(tag & 7);
                if (wtype == 0)
                {
                    ReadVarint(data, size, pos);
                }
                else if (wtype == 2)
                {
                    uint64_t len = ReadVarint(data, size, pos);
                    if (pos + len > size) break;
                    if (field == 4)
                        nsOut.assign(reinterpret_cast<char const*>(data + pos), static_cast<size_t>(len));
                    else if (field == 6)
                        payloadOut.assign(reinterpret_cast<char const*>(data + pos), static_cast<size_t>(len));
                    pos += static_cast<size_t>(len);
                }
                else if (wtype == 5)
                {
                    pos += 4; // 32-bit (unused by CastMessage)
                }
                else if (wtype == 1)
                {
                    pos += 8; // 64-bit (unused by CastMessage)
                }
                else
                {
                    break;
                }
            }
        }

        std::string ToUtf8(winrt::hstring const& s) { return winrt::to_string(s); }

        bool EndsWith(std::string const& s, char const* suffix)
        {
            std::string suf{ suffix };
            return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
        }
    }

    CastEngine::~CastEngine()
    {
        TeardownInternal(false);
    }

    bool CastEngine::IsMediaChannelOpen() const
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_mediaChannelOpen && !m_transportId.empty();
    }

    winrt::hstring CastEngine::CurrentHost() const
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        return m_host;
    }

    // ---- Discovery (Windows DNS-SD; uses the OS resolver -> multi-NIC safe) --

    namespace
    {
        struct BrowseCtx
        {
            std::mutex mtx;
            std::set<std::wstring> instances;
        };

        VOID WINAPI BrowseCallback(DWORD Status, PVOID pQueryContext, PDNS_RECORD pDnsRecord)
        {
            auto* ctx = reinterpret_cast<BrowseCtx*>(pQueryContext);
            if (Status == ERROR_SUCCESS && pDnsRecord && ctx)
            {
                for (PDNS_RECORD r = pDnsRecord; r; r = r->pNext)
                {
                    if (r->wType == DNS_TYPE_PTR && r->Data.PTR.pNameHost)
                    {
                        std::lock_guard<std::mutex> lock(ctx->mtx);
                        ctx->instances.insert(r->Data.PTR.pNameHost);
                    }
                }
            }
            if (pDnsRecord)
            {
                DnsRecordListFree(pDnsRecord, DnsFreeRecordList);
            }
        }

        struct ResolveCtx
        {
            HANDLE done{ nullptr };
            CastDevice device;
            bool ok{ false };
        };

        VOID WINAPI ResolveCallback(DWORD Status, PVOID pQueryContext, PDNS_SERVICE_INSTANCE pInstance)
        {
            auto* ctx = reinterpret_cast<ResolveCtx*>(pQueryContext);
            if (Status == ERROR_SUCCESS && pInstance && ctx)
            {
                if (pInstance->ip4Address)
                {
                    DWORD ip = *pInstance->ip4Address;
                    wchar_t buf[32];
                    swprintf_s(buf, L"%u.%u.%u.%u",
                        ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
                    ctx->device.host = buf;
                }
                else if (pInstance->pszHostName)
                {
                    ctx->device.host = pInstance->pszHostName;
                }
                ctx->device.port = pInstance->wPort ? pInstance->wPort : 8009;

                std::wstring fn, id;
                for (DWORD i = 0; i < pInstance->dwPropertyCount; ++i)
                {
                    if (!pInstance->keys || !pInstance->keys[i] || !pInstance->values) continue;
                    std::wstring key = pInstance->keys[i];
                    std::wstring val = pInstance->values[i] ? pInstance->values[i] : L"";
                    if (_wcsicmp(key.c_str(), L"fn") == 0) fn = val;
                    else if (_wcsicmp(key.c_str(), L"id") == 0) id = val;
                }
                std::wstring label = pInstance->pszInstanceName ? pInstance->pszInstanceName : L"";
                auto dot = label.find(L'.');
                if (dot != std::wstring::npos) label = label.substr(0, dot);

                ctx->device.name = !fn.empty() ? fn : label;
                ctx->device.id = !id.empty() ? id : (pInstance->pszInstanceName ? pInstance->pszInstanceName : ctx->device.host);
                ctx->ok = !ctx->device.host.empty();
            }
            if (pInstance)
            {
                DnsServiceFreeInstance(pInstance);
            }
            if (ctx && ctx->done)
            {
                SetEvent(ctx->done);
            }
        }
    }

    std::vector<CastDevice> CastEngine::Discover(uint32_t timeoutMs)
    {
        std::vector<CastDevice> result;

        BrowseCtx browse;
        DNS_SERVICE_BROWSE_REQUEST req{};
        req.Version = DNS_QUERY_REQUEST_VERSION1;
        req.InterfaceIndex = 0;
        req.QueryName = L"_googlecast._tcp.local";
        req.pBrowseCallback = &BrowseCallback;
        req.pQueryContext = &browse;

        DNS_SERVICE_CANCEL cancel{};
        DNS_STATUS status = DnsServiceBrowse(&req, &cancel);
        if (status != DNS_REQUEST_PENDING && status != ERROR_SUCCESS)
        {
            return result;
        }

        Sleep(timeoutMs);
        DnsServiceBrowseCancel(&cancel);

        std::set<std::wstring> instances;
        {
            std::lock_guard<std::mutex> lock(browse.mtx);
            instances = browse.instances;
        }

        std::set<std::wstring> seen;
        for (auto const& inst : instances)
        {
            ResolveCtx rctx;
            rctx.done = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            std::wstring queryName = inst;
            DNS_SERVICE_RESOLVE_REQUEST rreq{};
            rreq.Version = DNS_QUERY_REQUEST_VERSION1;
            rreq.InterfaceIndex = 0;
            rreq.QueryName = const_cast<PWSTR>(queryName.c_str());
            rreq.pResolveCompletionCallback = &ResolveCallback;
            rreq.pQueryContext = &rctx;

            DNS_SERVICE_CANCEL rcancel{};
            DNS_STATUS rs = DnsServiceResolve(&rreq, &rcancel);
            if (rs == DNS_REQUEST_PENDING || rs == ERROR_SUCCESS)
            {
                WaitForSingleObject(rctx.done, 2000);
                DnsServiceResolveCancel(&rcancel);
            }
            CloseHandle(rctx.done);

            if (rctx.ok && seen.insert(rctx.device.id).second)
            {
                result.push_back(rctx.device);
            }
        }
        return result;
    }

    // ---- Connection / protocol ---------------------------------------------

    IAsyncAction CastEngine::ConnectAsync(winrt::hstring host)
    {
        if (m_connected.load())
        {
            Disconnect();
        }

        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_host = host;
            m_transportId.clear();
            m_sessionId.clear();
            m_mediaSessionId = 0;
            m_mediaChannelOpen = false;
            m_loadSent = false;
            m_pendingLoadJson.clear();
        }
        m_requestId = 0;

        StreamSocket socket;
        // Enable TCP keepalive on the long-lived Cast control socket so
        // a sleeping device / idle-killing firewall doesn't leave the
        // app stuck on a half-open connection. Default keepalive probes
        // wake the read loop when the peer is unreachable.
        socket.Control().KeepAlive(true);
        auto ignorable = socket.Control().IgnorableServerCertificateErrors();
        ignorable.Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::Untrusted);
        ignorable.Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::InvalidName);
        ignorable.Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::IncompleteChain);
        ignorable.Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::RevocationInformationMissing);
        ignorable.Append(winrt::Windows::Security::Cryptography::Certificates::ChainValidationResult::Expired);

        try
        {
            co_await socket.ConnectAsync(
                HostName(host),
                L"8009",
                SocketProtectionLevel::Tls12);
        }
        catch (...)
        {
            co_return;
        }

        m_socket = socket;
        m_writer = DataWriter(socket.OutputStream());
        m_reader = DataReader(socket.InputStream());
        m_reader.InputStreamOptions(InputStreamOptions::None);

        m_connected = true;
        m_running = true;

        SendJson(kNsConnection, "receiver-0", R"({"type":"CONNECT"})");

        WDJ::JsonObject launch;
        launch.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"LAUNCH"));
        launch.Insert(L"appId", WDJ::JsonValue::CreateStringValue(L"CC1AD845"));
        launch.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsReceiver, "receiver-0", ToUtf8(launch.Stringify()));

        StartHeartbeat();
        ReadLoopAsync();
        co_return;
    }

    void CastEngine::StartHeartbeat()
    {
        using namespace winrt::Windows::System::Threading;
        m_heartbeat = ThreadPoolTimer::CreatePeriodicTimer(
            [this](ThreadPoolTimer const&)
            {
                if (m_connected.load())
                {
                    SendJson(kNsHeartbeat, "receiver-0", R"({"type":"PING"})");
                }
            },
            std::chrono::seconds(5));
    }

    IAsyncAction CastEngine::ReadLoopAsync()
    {
        auto reader = m_reader;
        try
        {
            while (m_running.load())
            {
                uint32_t got = co_await reader.LoadAsync(4);
                if (got < 4) break;
                uint32_t len =
                    (static_cast<uint32_t>(reader.ReadByte()) << 24) |
                    (static_cast<uint32_t>(reader.ReadByte()) << 16) |
                    (static_cast<uint32_t>(reader.ReadByte()) << 8) |
                    static_cast<uint32_t>(reader.ReadByte());
                if (len == 0 || len > 8 * 1024 * 1024) break;

                uint32_t gotBody = co_await reader.LoadAsync(len);
                if (gotBody < len) break;

                std::vector<uint8_t> body(len);
                reader.ReadBytes(winrt::array_view<uint8_t>(body));

                std::string ns;
                std::string payload;
                DecodeCastMessage(body.data(), body.size(), ns, payload);
                if (!ns.empty())
                {
                    HandleMessage(ns, payload);
                }
            }
        }
        catch (...)
        {
        }
        TeardownInternal(true);
        co_return;
    }

    void CastEngine::HandleMessage(std::string const& ns, std::string const& payloadUtf8)
    {
        WDJ::JsonObject json{ nullptr };
        if (!payloadUtf8.empty())
        {
            WDJ::JsonObject::TryParse(winrt::to_hstring(payloadUtf8), json);
        }
        auto typeOf = [](WDJ::JsonObject const& o) -> winrt::hstring
        {
            if (o && o.HasKey(L"type"))
                return o.GetNamedString(L"type", L"");
            return winrt::hstring{};
        };

        if (EndsWith(ns, "tp.heartbeat"))
        {
            if (json && typeOf(json) == L"PING")
            {
                SendJson(kNsHeartbeat, "receiver-0", R"({"type":"PONG"})");
            }
            return;
        }

        if (EndsWith(ns, "tp.connection"))
        {
            if (json && typeOf(json) == L"CLOSE")
            {
                TeardownInternal(true);
            }
            return;
        }

        if (EndsWith(ns, "cast.receiver"))
        {
            if (!json || typeOf(json) != L"RECEIVER_STATUS") return;
            try
            {
                auto statusObj = json.GetNamedObject(L"status", nullptr);
                if (!statusObj) return;
                auto apps = statusObj.GetNamedArray(L"applications", nullptr);
                if (!apps) return;
                for (uint32_t i = 0; i < apps.Size(); ++i)
                {
                    auto app = apps.GetObjectAt(i);
                    auto transportId = app.GetNamedString(L"transportId", L"");
                    auto sessionId = app.GetNamedString(L"sessionId", L"");
                    if (transportId.empty()) continue;
                    std::string transportForConnect;
                    std::string pendingLoadJson;
                    {
                        std::lock_guard<std::mutex> lock(m_stateMutex);
                        m_transportId = ToUtf8(transportId);
                        m_sessionId = ToUtf8(sessionId);
                        if (!m_mediaChannelOpen)
                        {
                            m_mediaChannelOpen = true;
                            transportForConnect = m_transportId;
                            if (!m_pendingLoadJson.empty() && !m_loadSent)
                            {
                                pendingLoadJson = m_pendingLoadJson;
                                m_loadSent = true;
                            }
                        }
                    }
                    if (!transportForConnect.empty())
                    {
                        SendJson(kNsConnection, transportForConnect, R"({"type":"CONNECT"})");
                    }
                    if (!pendingLoadJson.empty())
                    {
                        SendJson(kNsMedia, transportForConnect, pendingLoadJson);
                    }
                    break;
                }
            }
            catch (...)
            {
            }
            return;
        }

        if (EndsWith(ns, "cast.media"))
        {
            if (!json || typeOf(json) != L"MEDIA_STATUS") return;
            try
            {
                auto arr = json.GetNamedArray(L"status", nullptr);
                if (!arr || arr.Size() == 0) return;
                auto s0 = arr.GetObjectAt(0);
                {
                    std::lock_guard<std::mutex> lock(m_stateMutex);
                    m_mediaSessionId = static_cast<int>(s0.GetNamedNumber(L"mediaSessionId", 0));
                }
                winrt::hstring state = s0.GetNamedString(L"playerState", L"");
                double cur = s0.GetNamedNumber(L"currentTime", 0.0);
                double dur = 0.0;
                if (s0.HasKey(L"media"))
                {
                    auto m = s0.GetNamedObject(L"media", nullptr);
                    if (m) dur = m.GetNamedNumber(L"duration", 0.0);
                }
                winrt::hstring idle = s0.GetNamedString(L"idleReason", L"");
                if (StatusChanged)
                {
                    StatusChanged(state, cur, dur);
                }
                if (state == L"IDLE" && idle == L"FINISHED" && Ended)
                {
                    Ended();
                }
            }
            catch (...)
            {
            }
            return;
        }
    }

    void CastEngine::EnqueueFrame(std::vector<uint8_t> frame)
    {
        {
            std::lock_guard<std::mutex> lock(m_outboxMutex);
            m_outbox.push_back(std::move(frame));
        }
        if (!m_pumpRunning.exchange(true))
        {
            PumpOutboxAsync();
        }
    }

    winrt::fire_and_forget CastEngine::PumpOutboxAsync()
    {
        auto writer = m_writer;
        if (!writer)
        {
            m_pumpRunning = false;
            co_return;
        }
        try
        {
            while (true)
            {
                std::vector<uint8_t> frame;
                {
                    std::lock_guard<std::mutex> lock(m_outboxMutex);
                    if (m_outbox.empty())
                    {
                        m_pumpRunning = false;
                        co_return;
                    }
                    frame = std::move(m_outbox.front());
                    m_outbox.pop_front();
                }
                writer.WriteBytes(winrt::array_view<uint8_t const>(frame));
                co_await writer.StoreAsync();
            }
        }
        catch (...)
        {
            m_pumpRunning = false;
            TeardownInternal(true);
        }
    }

    void CastEngine::SendJson(std::string const& ns, std::string const& destination, std::string const& jsonPayload)
    {
        if (!m_connected.load() && ns != kNsConnection && ns != kNsReceiver && ns != kNsHeartbeat)
        {
            return;
        }
        EnqueueFrame(EncodeCastMessage(ns, destination, jsonPayload));
    }

    IAsyncAction CastEngine::LoadAsync(
        winrt::hstring url,
        winrt::hstring title,
        winrt::hstring artist,
        winrt::hstring artworkUrl)
    {
        WDJ::JsonObject media;
        media.Insert(L"contentId", WDJ::JsonValue::CreateStringValue(url));
        media.Insert(L"contentType", WDJ::JsonValue::CreateStringValue(L"audio/mpeg"));
        media.Insert(L"streamType", WDJ::JsonValue::CreateStringValue(L"BUFFERED"));

        WDJ::JsonObject metadata;
        metadata.Insert(L"metadataType", WDJ::JsonValue::CreateNumberValue(3));
        metadata.Insert(L"title", WDJ::JsonValue::CreateStringValue(title));
        metadata.Insert(L"artist", WDJ::JsonValue::CreateStringValue(artist));
        if (!artworkUrl.empty())
        {
            WDJ::JsonArray images;
            WDJ::JsonObject img;
            img.Insert(L"url", WDJ::JsonValue::CreateStringValue(artworkUrl));
            images.Append(img);
            metadata.Insert(L"images", images);
        }
        media.Insert(L"metadata", metadata);

        WDJ::JsonObject load;
        load.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"LOAD"));
        load.Insert(L"autoplay", WDJ::JsonValue::CreateBooleanValue(true));
        load.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        load.Insert(L"media", media);

        std::string loadJson = ToUtf8(load.Stringify());
        std::string transportId;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (m_mediaChannelOpen && !m_transportId.empty())
            {
                transportId = m_transportId;
                m_loadSent = true;
            }
            else
            {
                m_pendingLoadJson = loadJson;
                m_loadSent = false;
            }
        }
        if (!transportId.empty())
        {
            SendJson(kNsMedia, transportId, loadJson);
        }
        co_return;
    }

    void CastEngine::Play()
    {
        std::string transportId;
        int mediaSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            transportId = m_transportId;
            mediaSessionId = m_mediaSessionId;
        }
        if (transportId.empty()) return;
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"PLAY"));
        o.Insert(L"mediaSessionId", WDJ::JsonValue::CreateNumberValue(mediaSessionId));
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsMedia, transportId, ToUtf8(o.Stringify()));
    }

    void CastEngine::Pause()
    {
        std::string transportId;
        int mediaSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            transportId = m_transportId;
            mediaSessionId = m_mediaSessionId;
        }
        if (transportId.empty()) return;
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"PAUSE"));
        o.Insert(L"mediaSessionId", WDJ::JsonValue::CreateNumberValue(mediaSessionId));
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsMedia, transportId, ToUtf8(o.Stringify()));
    }

    void CastEngine::Stop()
    {
        std::string transportId;
        int mediaSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            transportId = m_transportId;
            mediaSessionId = m_mediaSessionId;
        }
        if (transportId.empty()) return;
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"STOP"));
        o.Insert(L"mediaSessionId", WDJ::JsonValue::CreateNumberValue(mediaSessionId));
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsMedia, transportId, ToUtf8(o.Stringify()));
    }

    void CastEngine::Seek(double seconds)
    {
        std::string transportId;
        int mediaSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            transportId = m_transportId;
            mediaSessionId = m_mediaSessionId;
        }
        if (transportId.empty()) return;
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"SEEK"));
        o.Insert(L"mediaSessionId", WDJ::JsonValue::CreateNumberValue(mediaSessionId));
        o.Insert(L"currentTime", WDJ::JsonValue::CreateNumberValue(seconds));
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsMedia, transportId, ToUtf8(o.Stringify()));
    }

    void CastEngine::RequestStatus()
    {
        std::string transportId;
        int mediaSessionId = 0;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            if (!m_mediaChannelOpen || m_transportId.empty()) return;
            transportId = m_transportId;
            mediaSessionId = m_mediaSessionId;
        }
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"GET_STATUS"));
        if (mediaSessionId > 0)
        {
            o.Insert(L"mediaSessionId", WDJ::JsonValue::CreateNumberValue(mediaSessionId));
        }
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsMedia, transportId, ToUtf8(o.Stringify()));
    }

    void CastEngine::SetVolume(double level)
    {
        if (level < 0.0) level = 0.0;
        if (level > 1.0) level = 1.0;
        WDJ::JsonObject vol;
        vol.Insert(L"level", WDJ::JsonValue::CreateNumberValue(level));
        WDJ::JsonObject o;
        o.Insert(L"type", WDJ::JsonValue::CreateStringValue(L"SET_VOLUME"));
        o.Insert(L"volume", vol);
        o.Insert(L"requestId", WDJ::JsonValue::CreateNumberValue(NextRequestId()));
        SendJson(kNsReceiver, "receiver-0", ToUtf8(o.Stringify()));
    }

    void CastEngine::Disconnect()
    {
        if (m_connected.load())
        {
            try
            {
                SendJson(kNsConnection, "receiver-0", R"({"type":"CLOSE"})");
            }
            catch (...)
            {
            }
        }
        TeardownInternal(false);
    }

    void CastEngine::TeardownInternal(bool notify)
    {
        bool wasConnected = m_connected.exchange(false);
        m_running = false;

        if (m_heartbeat)
        {
            try { m_heartbeat.Cancel(); } catch (...) {}
            m_heartbeat = nullptr;
        }
        if (m_socket)
        {
            try { m_socket.Close(); } catch (...) {}
            m_socket = nullptr;
        }
        m_writer = nullptr;
        m_reader = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_stateMutex);
            m_mediaChannelOpen = false;
            m_transportId.clear();
            m_sessionId.clear();
            m_mediaSessionId = 0;
            m_loadSent = false;
            m_pendingLoadJson.clear();
        }
        {
            std::lock_guard<std::mutex> lock(m_outboxMutex);
            m_outbox.clear();
        }

        if (notify && wasConnected && Disconnected)
        {
            Disconnected();
        }
    }
}
