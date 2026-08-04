#include "pch.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincred.h>

#include "Backend/CredentialStore.h"
#include "Backend/SettingsManager.h"
#include "Backend/AccountClient.h"
#include "Backend/AccountSessionGateway.h"
#include "Backend/AccountSessionService.h"
#include "Backend/BuildConfig.h"
#include "Backend/AccountUrlPolicy.h"
#include "Backend/ProfileIdentity.h"
#include "Backend/RemoteMusicService.h"
#include "Backend/LoopbackCallback.h"
#include "Backend/Pkce.h"
#include "Backend/DatabaseAccountSchema.h"
#include "Backend/PlaybackHistoryQualifier.h"
#include "Backend/ProviderHelpers.h"
#include "Backend/CatalogParser.h"
#include "Backend/CatalogPresentation.h"
#include "Backend/HistoryImport.h"
#include "Backend/StreamCache.h"
#include "Backend/UserDataOperationGate.h"
#include "ThirdParty/sqlite/sqlite3.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace account = LastMusicPlayer::Backend;

namespace
{
    void Expect(bool condition, char const* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    bool EnsureTestWinsock()
    {
        static std::once_flag flag;
        static bool initialized{};
        std::call_once(flag, []
        {
            WSADATA data{};
            initialized = ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
        });
        return initialized;
    }

    bool SendAll(SOCKET socket, std::string_view payload)
    {
        std::size_t sent{};
        while (sent < payload.size())
        {
            auto written = ::send(
                socket,
                payload.data() + sent,
                static_cast<int>(payload.size() - sent),
                0);
            if (written <= 0)
            {
                return false;
            }
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }

    class TestHttpServer final
    {
    public:
        explicit TestHttpServer(
            std::string response,
            std::chrono::milliseconds acceptTimeout = std::chrono::milliseconds(1500))
            : m_response(std::move(response)),
              m_acceptTimeout(acceptTimeout)
        {
            Expect(EnsureTestWinsock(), "test HTTP server could not initialize Winsock");

            auto listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            Expect(listener != INVALID_SOCKET, "test HTTP server could not create a listener");

            BOOL exclusive = TRUE;
            if (::setsockopt(
                    listener,
                    SOL_SOCKET,
                    SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<char const*>(&exclusive),
                    sizeof(exclusive)) != 0)
            {
                ::closesocket(listener);
                throw std::runtime_error("test HTTP server could not reserve its port");
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (::bind(
                    listener,
                    reinterpret_cast<sockaddr*>(&address),
                    sizeof(address)) != 0
                || ::listen(listener, 1) != 0)
            {
                ::closesocket(listener);
                throw std::runtime_error("test HTTP server could not bind its listener");
            }

            int length = sizeof(address);
            if (::getsockname(
                    listener,
                    reinterpret_cast<sockaddr*>(&address),
                    &length) != 0)
            {
                ::closesocket(listener);
                throw std::runtime_error("test HTTP server could not read its port");
            }

            m_port = ntohs(address.sin_port);
            m_listener.store(static_cast<std::uintptr_t>(listener));
            m_thread = std::thread([this]
            {
                Run();
            });
        }

        ~TestHttpServer()
        {
            Stop();
            Join();
        }

        std::wstring Url() const
        {
            return L"http://127.0.0.1:" + std::to_wstring(m_port);
        }

        bool WasCalled() const noexcept
        {
            return m_called.load();
        }

        std::string Request() const
        {
            std::lock_guard guard{ m_mutex };
            return m_request;
        }

        void Join()
        {
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }

    private:
        static constexpr std::uintptr_t InvalidSocket =
            static_cast<std::uintptr_t>(INVALID_SOCKET);

        void Stop() noexcept
        {
            auto value = m_listener.exchange(InvalidSocket);
            if (value != InvalidSocket)
            {
                auto listener = static_cast<SOCKET>(value);
                ::shutdown(listener, SD_BOTH);
                ::closesocket(listener);
            }
        }

        void Run()
        {
            auto value = m_listener.load();
            if (value == InvalidSocket)
            {
                return;
            }
            auto listener = static_cast<SOCKET>(value);

            timeval timeout{};
            timeout.tv_sec = static_cast<long>(m_acceptTimeout.count() / 1000);
            timeout.tv_usec = static_cast<long>((m_acceptTimeout.count() % 1000) * 1000);
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(listener, &readSet);
            auto selected = ::select(0, &readSet, nullptr, nullptr, &timeout);
            if (selected <= 0)
            {
                Stop();
                return;
            }

            auto accepted = ::accept(listener, nullptr, nullptr);
            Stop();
            if (accepted == INVALID_SOCKET)
            {
                return;
            }

            DWORD receiveTimeout = 1500;
            ::setsockopt(
                accepted,
                SOL_SOCKET,
                SO_RCVTIMEO,
                reinterpret_cast<char const*>(&receiveTimeout),
                sizeof(receiveTimeout));

            std::string request;
            std::array<char, 1024> buffer{};
            while (request.size() < 16 * 1024
                && request.find("\r\n\r\n") == std::string::npos)
            {
                auto received = ::recv(
                    accepted,
                    buffer.data(),
                    static_cast<int>(buffer.size()),
                    0);
                if (received <= 0)
                {
                    break;
                }
                request.append(buffer.data(), static_cast<std::size_t>(received));
            }

            {
                std::lock_guard guard{ m_mutex };
                m_request = std::move(request);
            }
            m_called.store(true);
            SendAll(accepted, m_response);
            ::shutdown(accepted, SD_BOTH);
            ::closesocket(accepted);
        }

        std::string m_response;
        std::chrono::milliseconds m_acceptTimeout;
        std::atomic<std::uintptr_t> m_listener{ InvalidSocket };
        std::uint16_t m_port{};
        mutable std::mutex m_mutex;
        std::string m_request;
        std::atomic<bool> m_called{};
        std::thread m_thread;
    };

    SOCKET ConnectToLoopback(winrt::hstring const& redirectUri)
    {
        Expect(EnsureTestWinsock(), "callback client could not initialize Winsock");
        winrt::Windows::Foundation::Uri uri{ redirectUri };
        auto ipv6 = uri.Host() == L"::1" || uri.Host() == L"[::1]";
        auto socket = ::socket(
            ipv6 ? AF_INET6 : AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP);
        Expect(socket != INVALID_SOCKET, "callback client could not create a socket");

        int connected{};
        if (ipv6)
        {
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_port = htons(static_cast<u_short>(uri.Port()));
            address.sin6_addr = in6addr_loopback;
            connected = ::connect(
                socket,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address));
        }
        else
        {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(static_cast<u_short>(uri.Port()));
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            connected = ::connect(
                socket,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address));
        }
        if (connected != 0)
        {
            ::closesocket(socket);
            throw std::runtime_error("callback client could not connect");
        }
        return socket;
    }

    std::string CallbackAuthority(winrt::hstring const& redirectUri)
    {
        winrt::Windows::Foundation::Uri uri{ redirectUri };
        std::wstring authority = uri.Host() == L"::1" || uri.Host() == L"[::1]"
            ? L"[::1]"
            : L"127.0.0.1";
        authority += L":" + std::to_wstring(uri.Port());
        return winrt::to_string(authority);
    }

    std::string SendCallbackRequest(
        winrt::hstring const& redirectUri,
        std::string const& target)
    {
        auto socket = ConnectToLoopback(redirectUri);
        auto request = "GET " + target + " HTTP/1.1\r\nHost: "
            + CallbackAuthority(redirectUri)
            + "\r\nConnection: close\r\n\r\n";
        Expect(SendAll(socket, request), "callback client could not send its request");

        DWORD receiveTimeout = 2500;
        ::setsockopt(
            socket,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<char const*>(&receiveTimeout),
            sizeof(receiveTimeout));
        std::string response;
        std::array<char, 1024> buffer{};
        for (;;)
        {
            auto received = ::recv(
                socket,
                buffer.data(),
                static_cast<int>(buffer.size()),
                0);
            if (received <= 0)
            {
                break;
            }
            response.append(buffer.data(), static_cast<std::size_t>(received));
        }
        ::closesocket(socket);
        return response;
    }

    struct FakeCredentialApi final : account::ICredentialApi
    {
        bool WriteGeneric(
            std::wstring const& target,
            std::vector<std::uint8_t> const& secret,
            std::uint32_t persistence) noexcept override
        {
            lastWriteTarget = target;
            lastPersistence = persistence;
            if (failWrite)
            {
                return false;
            }
            values[target] = secret;
            return true;
        }

        std::optional<std::vector<std::uint8_t>> ReadGeneric(
            std::wstring const& target) noexcept override
        {
            if (corruptNextRead)
            {
                corruptNextRead = false;
                return std::vector<std::uint8_t>{ 'w', 'r', 'o', 'n', 'g' };
            }
            auto found = values.find(target);
            if (found == values.end())
            {
                return std::nullopt;
            }
            return found->second;
        }

        bool DeleteGeneric(std::wstring const& target) noexcept override
        {
            lastDeleteTarget = target;
            if (failDelete)
            {
                return false;
            }
            values.erase(target);
            return true;
        }

        std::map<std::wstring, std::vector<std::uint8_t>> values;
        std::wstring lastWriteTarget;
        std::wstring lastDeleteTarget;
        std::uint32_t lastPersistence{};
        bool failWrite{};
        bool failDelete{};
        bool corruptNextRead{};
    };

    struct AsyncGate final
    {
        AsyncGate()
            : Entered(EnteredSignal.get_future().share()),
              Released(ReleaseSignal.get_future().share())
        {
        }

        void Reach()
        {
            EnteredSignal.set_value();
            Released.wait();
        }

        void Release()
        {
            ReleaseSignal.set_value();
        }

        std::promise<void> EnteredSignal;
        std::shared_future<void> Entered;
        std::promise<void> ReleaseSignal;
        std::shared_future<void> Released;
    };

    struct ScriptedStreamCacheTransport final : account::IStreamCacheTransport
    {
        struct DownloadScript
        {
            std::shared_ptr<AsyncGate> Gate;
            std::string Payload;
            winrt::hstring Extension;
        };

        void Queue(DownloadScript script)
        {
            std::lock_guard guard{ Mutex };
            Scripts.push_back(std::move(script));
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DownloadAsync(
            std::wstring streamUrl,
            std::filesystem::path partPath) override
        {
            (void)streamUrl;
            co_await winrt::resume_background();

            DownloadScript script;
            {
                std::lock_guard guard{ Mutex };
                if (Scripts.empty())
                {
                    throw winrt::hresult_error(E_FAIL, L"No scripted stream download.");
                }
                script = std::move(Scripts.front());
                Scripts.pop_front();
            }

            if (script.Gate)
            {
                script.Gate->Reach();
            }

            std::ofstream output{ partPath, std::ios::binary | std::ios::trunc };
            if (!output)
            {
                throw winrt::hresult_error(E_FAIL, L"Could not write scripted stream data.");
            }
            output.write(script.Payload.data(), static_cast<std::streamsize>(script.Payload.size()));
            output.close();
            if (!output)
            {
                throw winrt::hresult_error(E_FAIL, L"Could not finish scripted stream data.");
            }
            co_return script.Extension;
        }

        std::mutex Mutex;
        std::deque<DownloadScript> Scripts;
    };

    struct ScriptedAccountSessionGateway final : account::IAccountSessionGateway
    {
        struct AcquireScript
        {
            winrt::hstring Bearer;
            std::shared_ptr<AsyncGate> Gate;
            std::optional<HRESULT> Error;
        };

        struct ProfileScript
        {
            account::AccountProfile Profile;
            std::shared_ptr<AsyncGate> Gate;
            std::optional<HRESULT> Error;
        };

        struct LogoutScript
        {
            std::shared_ptr<AsyncGate> Gate;
            std::optional<HRESULT> Error;
        };

        bool IsConfigured() const noexcept override
        {
            return Configured;
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring>
            AcquireBearerSessionAsync(std::uint64_t intent) override
        {
            co_await winrt::resume_background();
            AcquireScript script;
            {
                std::lock_guard guard{ Mutex };
                if (AcquireScripts.empty())
                {
                    throw winrt::hresult_error(E_FAIL, L"No scripted sign-in result.");
                }
                script = std::move(AcquireScripts.front());
                AcquireScripts.pop_front();
                LastAcquireIntent = intent;
            }
            if (script.Gate)
            {
                script.Gate->Reach();
            }
            if (script.Error)
            {
                throw winrt::hresult_error(*script.Error, L"Scripted sign-in failure.");
            }
            co_return script.Bearer;
        }

        void CancelAcquireBearerSession(std::uint64_t throughIntent) noexcept override
        {
            {
                std::lock_guard guard{ Mutex };
                LastCanceledIntent = (std::max)(LastCanceledIntent, throughIntent);
            }
            CancellationChanged.notify_all();
        }

        winrt::Windows::Foundation::IAsyncAction GetProfileAsync(
            winrt::hstring const& bearerSession,
            std::function<void(account::AccountProfile const&)> receiveProfile) override
        {
            co_await winrt::resume_background();
            ProfileScript script;
            {
                std::lock_guard guard{ Mutex };
                auto found = ProfileScripts.find(std::wstring(bearerSession.c_str()));
                if (found == ProfileScripts.end())
                {
                    throw winrt::hresult_error(E_FAIL, L"No scripted profile result.");
                }
                script = found->second;
            }
            if (script.Gate)
            {
                script.Gate->Reach();
            }
            if (script.Error)
            {
                throw winrt::hresult_error(*script.Error, L"Scripted profile failure.");
            }
            receiveProfile(script.Profile);
        }

        winrt::Windows::Foundation::IAsyncAction LogoutAsync(
            winrt::hstring const& bearerSession) override
        {
            co_await winrt::resume_background();
            LogoutScript script;
            {
                std::lock_guard guard{ Mutex };
                auto found = LogoutScripts.find(std::wstring(bearerSession.c_str()));
                if (found != LogoutScripts.end())
                {
                    script = found->second;
                }
            }
            if (script.Gate)
            {
                script.Gate->Reach();
            }
            if (script.Error)
            {
                throw winrt::hresult_error(*script.Error, L"Scripted logout failure.");
            }
        }

        void QueueAcquire(
            winrt::hstring const& bearer,
            std::shared_ptr<AsyncGate> gate = nullptr,
            std::optional<HRESULT> error = std::nullopt)
        {
            std::lock_guard guard{ Mutex };
            AcquireScripts.push_back({ bearer, std::move(gate), error });
        }

        void SetProfile(
            winrt::hstring const& bearer,
            account::AccountProfile profile,
            std::shared_ptr<AsyncGate> gate = nullptr,
            std::optional<HRESULT> error = std::nullopt)
        {
            std::lock_guard guard{ Mutex };
            ProfileScripts[std::wstring(bearer.c_str())] = {
                std::move(profile),
                std::move(gate),
                error
            };
        }

        void SetLogout(
            winrt::hstring const& bearer,
            std::shared_ptr<AsyncGate> gate = nullptr,
            std::optional<HRESULT> error = std::nullopt)
        {
            std::lock_guard guard{ Mutex };
            LogoutScripts[std::wstring(bearer.c_str())] = { std::move(gate), error };
        }

        std::uint64_t AcquireIntent() const
        {
            std::lock_guard guard{ Mutex };
            return LastAcquireIntent;
        }

        bool WaitForCancellationAfter(std::uint64_t intent)
        {
            std::unique_lock lock{ Mutex };
            return CancellationChanged.wait_for(
                lock,
                std::chrono::seconds(2),
                [this, intent]
                {
                    return LastCanceledIntent > intent;
                });
        }

        bool Configured{ true };
        mutable std::mutex Mutex;
        std::condition_variable CancellationChanged;
        std::deque<AcquireScript> AcquireScripts;
        std::map<std::wstring, ProfileScript> ProfileScripts;
        std::map<std::wstring, LogoutScript> LogoutScripts;
        std::uint64_t LastAcquireIntent{};
        std::uint64_t LastCanceledIntent{};
    };

    struct ScriptedAccountSyncTransport final : account::IAccountSyncTransport
    {
        enum class CallKind
        {
            Library,
            Like,
            History,
        };

        struct Script
        {
            winrt::hstring Payload;
            std::shared_ptr<AsyncGate> Gate;
            std::optional<HRESULT> Error;
        };

        struct Call
        {
            CallKind Kind{};
            std::wstring Bearer;
            std::wstring Body;
            bool Liked{};
            std::wstring RemoteId;
        };

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetLibraryAsync(
            winrt::hstring const& bearerSession) override
        {
            co_await winrt::resume_background();
            auto script = TakeScript(
                m_libraryScript,
                { CallKind::Library, bearerSession.c_str() });
            co_return Complete(script);
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> SetLikedAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& trackJson,
            bool liked,
            winrt::hstring const& remoteId) override
        {
            co_await winrt::resume_background();
            auto script = TakeScript(
                m_likeScript,
                {
                    CallKind::Like,
                    bearerSession.c_str(),
                    trackJson.c_str(),
                    liked,
                    remoteId.c_str()
                });
            co_return Complete(script);
        }

        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> PostHistoryBatchAsync(
            winrt::hstring const& bearerSession,
            winrt::hstring const& batchJson) override
        {
            co_await winrt::resume_background();
            auto script = TakeScript(
                m_historyScript,
                { CallKind::History, bearerSession.c_str(), batchJson.c_str() });
            co_return Complete(script);
        }

        void SetLibraryScript(Script script)
        {
            std::lock_guard guard{ m_mutex };
            m_libraryScript = std::move(script);
        }

        void SetLikeScript(Script script)
        {
            std::lock_guard guard{ m_mutex };
            m_likeScript = std::move(script);
        }

        void SetHistoryScript(Script script)
        {
            std::lock_guard guard{ m_mutex };
            m_historyScript = std::move(script);
        }

        std::vector<Call> Calls() const
        {
            std::lock_guard guard{ m_mutex };
            return m_calls;
        }

    private:
        Script TakeScript(std::optional<Script>& slot, Call call)
        {
            std::lock_guard guard{ m_mutex };
            if (!slot)
            {
                throw winrt::hresult_error(E_FAIL, L"No scripted sync response.");
            }
            m_calls.push_back(std::move(call));
            auto script = std::move(*slot);
            slot.reset();
            return script;
        }

        static winrt::hstring Complete(Script const& script)
        {
            if (script.Gate)
            {
                script.Gate->Reach();
            }
            if (script.Error)
            {
                throw winrt::hresult_error(*script.Error, L"Scripted sync failure.");
            }
            return script.Payload;
        }

        mutable std::mutex m_mutex;
        std::optional<Script> m_libraryScript;
        std::optional<Script> m_likeScript;
        std::optional<Script> m_historyScript;
        std::vector<Call> m_calls;
    };

    struct AccountSessionFixture final
    {
        AccountSessionFixture()
            : Api(std::make_shared<FakeCredentialApi>()),
              Credentials(Api),
              Service(Settings, Credentials, Gateway, OperationGate)
        {
            (void)Settings.Reset();
        }

        account::SettingsManager Settings;
        std::shared_ptr<FakeCredentialApi> Api;
        account::CredentialStore Credentials;
        ScriptedAccountSessionGateway Gateway;
        account::UserDataOperationGate OperationGate;
        account::AccountSessionService Service;
    };

    account::AccountProfile Profile(winrt::hstring const& owner)
    {
        account::AccountProfile profile;
        profile.Id = owner;
        profile.DisplayName = winrt::hstring(L"User " + std::wstring(owner.c_str()));
        profile.Username = owner;
        profile.PlanLabel = L"Test";
        return profile;
    }

    bool WaitUntilEntered(std::shared_ptr<AsyncGate> const& gate)
    {
        return gate->Entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    }

    void EstablishSession(
        AccountSessionFixture& fixture,
        winrt::hstring const& bearer,
        winrt::hstring const& owner)
    {
        fixture.Gateway.QueueAcquire(bearer);
        fixture.Gateway.SetProfile(bearer, Profile(owner));
        Expect(fixture.Service.SignInAsync().get(), "scripted sign-in did not succeed");
        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::Validated,
            "scripted sign-in did not publish a validated session");
        Expect(snapshot.Profile.Id == owner, "scripted sign-in published the wrong owner");
        Expect(fixture.Credentials.ReadAccountSession() == bearer,
            "scripted sign-in did not persist its bearer");
    }

    struct RemoteAccountFixture final
    {
        RemoteAccountFixture()
            : Remote(
                Session.Settings,
                Session.Credentials,
                Session.Service,
                Client,
                Transport)
        {
            EstablishSession(Session, L"bearer-a", L"owner-a");
            Expect(Remote.SetMode(account::RemoteAccessMode::Account),
                "remote fixture could not enter Account mode");
        }

        void ReplaceSession(
            winrt::hstring const& bearer = L"bearer-b",
            winrt::hstring const& owner = L"owner-b")
        {
            EstablishSession(Session, bearer, owner);
        }

        AccountSessionFixture Session;
        account::AccountClient Client;
        ScriptedAccountSyncTransport Transport;
        account::RemoteMusicService Remote;
    };

    void ExpectCanceled(
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> const& operation,
        char const* message)
    {
        try
        {
            (void)operation.get();
        }
        catch (winrt::hresult_canceled const&)
        {
            return;
        }
        throw std::runtime_error(message);
    }

    void ExpectTransportReached(
        std::shared_ptr<AsyncGate> const& gate,
        winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> const& operation,
        char const* message)
    {
        if (WaitUntilEntered(gate))
        {
            return;
        }

        gate->Release();
        try
        {
            (void)operation.get();
        }
        catch (...)
        {
        }
        throw std::runtime_error(message);
    }

    std::filesystem::path SettingsPath(std::filesystem::path const& localAppData)
    {
        return localAppData / L"Last Music Player" / L"Settings.json";
    }

    std::string ReadFile(std::filesystem::path const& path)
    {
        std::ifstream input{ path, std::ios::binary };
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    }

    void TestCredentialReadWriteDelete()
    {
        auto api = std::make_shared<FakeCredentialApi>();
        account::CredentialStore store{ api };

        Expect(store.WriteProviderApiKey(L"provider-secret"), "provider key write failed");
        Expect(store.ReadProviderApiKey() == L"provider-secret", "provider key read mismatch");
        Expect(api->lastPersistence == CRED_PERSIST_LOCAL_MACHINE, "credential persistence was not local-machine");
        auto providerTarget = api->lastWriteTarget;

        Expect(store.WriteAccountSession(L"account-session"), "account session write failed");
        Expect(store.ReadAccountSession() == L"account-session", "account session read mismatch");
        Expect(api->lastWriteTarget != providerTarget, "account and provider credentials share a target");
        Expect(
            store.DeleteAccountSessionIfMatches(L"different-session")
                == account::AccountCredentialDeleteResult::Mismatch,
            "conditional account delete did not reject a different bearer");
        Expect(store.ReadAccountSession() == L"account-session",
            "conditional account delete removed a different bearer");
        Expect(
            store.DeleteAccountSessionIfMatches(L"account-session")
                == account::AccountCredentialDeleteResult::Deleted,
            "conditional account delete did not remove the matching bearer");
        Expect(
            store.DeleteAccountSessionIfMatches(L"account-session")
                == account::AccountCredentialDeleteResult::NotFound,
            "conditional account delete did not report a missing bearer");

        Expect(store.WriteAccountSession(L"delete-failure"), "account session rewrite failed");
        api->failDelete = true;
        Expect(
            store.DeleteAccountSessionIfMatches(L"delete-failure")
                == account::AccountCredentialDeleteResult::Failed,
            "conditional account delete did not report storage failure");
        Expect(store.ReadAccountSession() == L"delete-failure",
            "failed conditional account delete removed the bearer");
        api->failDelete = false;

        Expect(store.DeleteProviderApiKey(), "provider key delete failed");
        Expect(store.ReadProviderApiKey().empty(), "provider key remained after delete");
        Expect(store.DeleteAccountSession(), "account session delete failed");
        Expect(store.ReadAccountSession().empty(), "account session remained after delete");
    }

    void TestSuccessfulLegacyMigration(std::filesystem::path const& localAppData)
    {
        account::SettingsManager settings;
        settings.SetString(L"ProviderApiKey", L"legacy-plaintext-secret");
        settings.SetString(L"ProviderBaseUrl", L"https://provider.example");

        auto api = std::make_shared<FakeCredentialApi>();
        account::CredentialStore store{ api };
        auto status = account::MigrateLegacyProviderApiKey(settings, store);

        Expect(status == account::CredentialMigrationStatus::Migrated, "legacy migration did not succeed");
        Expect(store.ReadProviderApiKey() == L"legacy-plaintext-secret", "migrated key was not verified");
        Expect(settings.GetString(L"ProviderApiKey", L"").empty(), "plaintext key remained in settings memory");

        auto serialized = ReadFile(SettingsPath(localAppData));
        Expect(serialized.find("legacy-plaintext-secret") == std::string::npos, "plaintext key remained in Settings.json");
        Expect(serialized.find("ProviderApiKey") == std::string::npos, "provider key field remained in Settings.json");
        Expect(serialized.find("provider.example") != std::string::npos, "non-secret provider URL was removed");
    }

    void TestFailedSecureWritePreservesLegacySource(std::filesystem::path const& localAppData)
    {
        account::SettingsManager settings;
        settings.SetString(L"ProviderApiKey", L"must-remain-until-secured");

        auto api = std::make_shared<FakeCredentialApi>();
        api->failWrite = true;
        account::CredentialStore store{ api };
        auto status = account::MigrateLegacyProviderApiKey(settings, store);

        Expect(status == account::CredentialMigrationStatus::SecureWriteFailed, "secure write failure was not reported");
        Expect(settings.GetString(L"ProviderApiKey", L"") == L"must-remain-until-secured", "failed migration removed source value");
        auto serialized = ReadFile(SettingsPath(localAppData));
        Expect(serialized.find("must-remain-until-secured") != std::string::npos, "failed migration changed Settings.json");

        auto message = account::CredentialMigrationMessage(status);
        Expect(std::wstring{ message.c_str() }.find(L"must-remain-until-secured") == std::wstring::npos, "safe error exposed the key");
    }

    void TestFailedVerificationPreservesLegacySource()
    {
        account::SettingsManager settings;
        settings.SetString(L"ProviderApiKey", L"verification-source");

        auto api = std::make_shared<FakeCredentialApi>();
        api->corruptNextRead = true;
        account::CredentialStore store{ api };
        auto status = account::MigrateLegacyProviderApiKey(settings, store);

        Expect(status == account::CredentialMigrationStatus::SecureVerificationFailed, "verification failure was not reported");
        Expect(settings.GetString(L"ProviderApiKey", L"") == L"verification-source", "verification failure removed source value");
    }
    void TestPkce()
    {
        auto challenge = account::CreatePkceChallenge(
            L"dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
        Expect(challenge == L"E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "RFC 7636 challenge mismatch");

        auto transaction = account::CreatePkceTransaction();
        Expect(transaction.Verifier.size() == 43, "PKCE verifier is not 32-byte base64url");
        Expect(transaction.State.size() == 32, "PKCE state is not 24-byte base64url");
        Expect(transaction.Challenge.size() == 43, "PKCE challenge length mismatch");
        auto base64UrlOnly = [](winrt::hstring const& value)
        {
            for (auto character : value)
            {
                if (!((character >= L'A' && character <= L'Z')
                    || (character >= L'a' && character <= L'z')
                    || (character >= L'0' && character <= L'9')
                    || character == L'-' || character == L'_'))
                {
                    return false;
                }
            }
            return true;
        };
        Expect(base64UrlOnly(transaction.Verifier), "PKCE verifier is not base64url");
        Expect(base64UrlOnly(transaction.State), "PKCE state is not base64url");
    }

    void TestLoopbackRequestParsing()
    {
        auto valid = account::ParseLoopbackHttpRequest(
            "GET /callback?code=code-value&state=state-value HTTP/1.1\r\nHost: 127.0.0.1:49152\r\nConnection: close\r\n\r\n",
            "127.0.0.1:49152",
            "/callback",
            "state-value");
        Expect(valid.Status == account::LoopbackCallbackStatus::Success, "valid IPv4 callback was rejected");
        Expect(valid.Code == L"code-value", "callback code mismatch");

        auto ipv6 = account::ParseLoopbackHttpRequest(
            "GET /callback?state=state-value&code=ipv6-code HTTP/1.1\r\nHost: [::1]:49153\r\n\r\n",
            "[::1]:49153",
            "/callback",
            "state-value");
        Expect(ipv6.Status == account::LoopbackCallbackStatus::Success, "valid IPv6 callback was rejected");

        auto wrongHost = account::ParseLoopbackHttpRequest(
            "GET /callback?code=x&state=s HTTP/1.1\r\nHost: localhost:49152\r\n\r\n",
            "127.0.0.1:49152",
            "/callback",
            "s");
        Expect(wrongHost.Status == account::LoopbackCallbackStatus::InvalidRequest, "hostname alias was accepted");

        auto duplicateState = account::ParseLoopbackHttpRequest(
            "GET /callback?code=x&state=s&state=s HTTP/1.1\r\nHost: 127.0.0.1:49152\r\n\r\n",
            "127.0.0.1:49152",
            "/callback",
            "s");
        Expect(duplicateState.Status == account::LoopbackCallbackStatus::InvalidRequest, "duplicate state was accepted");

        auto wrongPath = account::ParseLoopbackHttpRequest(
            "GET /callback/extra?code=x&state=s HTTP/1.1\r\nHost: 127.0.0.1:49152\r\n\r\n",
            "127.0.0.1:49152",
            "/callback",
            "s");
        Expect(wrongPath.Status == account::LoopbackCallbackStatus::InvalidRequest, "callback path confusion was accepted");

        auto canceled = account::ParseLoopbackHttpRequest(
            "GET /callback?error=access_denied&state=s HTTP/1.1\r\nHost: 127.0.0.1:49152\r\n\r\n",
            "127.0.0.1:49152",
            "/callback",
            "s");
        Expect(canceled.Status == account::LoopbackCallbackStatus::Canceled, "authorization cancellation was not recognized");
    }

    void TestConfiguredAccountBuild()
    {
        winrt::hstring apiOrigin{ account::BuildConfig::AccountApiOrigin };
        winrt::hstring frontendOrigin{ account::BuildConfig::AccountFrontendOrigin };
        winrt::hstring mediaOrigin{ account::BuildConfig::AccountMediaOrigin };

        account::AccountClient client;
        auto configured = !apiOrigin.empty();
        Expect(client.IsConfigured() == configured,
            "default account client did not match the build configuration");
#ifdef LAST_MUSIC_ACCOUNT_TEST_EXPECT_BLANK
        Expect(apiOrigin.empty() && frontendOrigin.empty() && mediaOrigin.empty(),
            "blank account test variant received an account origin");
#elif defined(LAST_MUSIC_ACCOUNT_TEST_EXPECT_API_ONLY)
        Expect(apiOrigin == L"https://api.account.example.test"
            && frontendOrigin.empty()
            && mediaOrigin == apiOrigin,
            "API-only account test variant received incorrect origins");
#elif defined(LAST_MUSIC_ACCOUNT_TEST_EXPECT_FULL)
        Expect(apiOrigin == L"https://api.account.example.test"
            && frontendOrigin == L"https://account.example.test"
            && mediaOrigin == L"https://media.account.example.test",
            "full account test variant received incorrect origins");
#endif
        if (!configured)
        {
            Expect(frontendOrigin.empty(),
                "blank account build unexpectedly configured a frontend origin");
            Expect(mediaOrigin.empty(),
                "blank account build unexpectedly configured a media origin");
            Expect(account::AccountManagementUrl().empty(),
                "blank account build exposed a management URL");
            return;
        }

        Expect(account::IsTrustedAccountOrigin(apiOrigin),
            "configured account API origin was not trusted");
        Expect(client.BaseOrigin() == account::NormalizeTrustedAccountOrigin(apiOrigin),
            "default account client did not normalize its configured origin");
        Expect(!mediaOrigin.empty(),
            "configured account build did not inherit or supply a media origin");
        Expect(account::IsTrustedAccountOrigin(mediaOrigin),
            "configured account media origin was not trusted");

        auto managementUrl = account::AccountManagementUrl();
        if (frontendOrigin.empty())
        {
            Expect(managementUrl.empty(),
                "API-only account build exposed a management URL");
        }
        else
        {
            std::wstring expectedManagementUrl{
                account::NormalizeTrustedAccountOrigin(frontendOrigin).c_str() };
            expectedManagementUrl += L"/account";
            Expect(managementUrl == winrt::hstring{ expectedManagementUrl },
                "configured frontend origin produced the wrong management URL");
        }
    }

    void TestAccountGatewayProtocol()
    {
        account::AccountClient client{ L"https://api.account.example.test/" };
        account::PkceTransaction transaction{
            L"verifier-must-not-appear",
            L"challenge value",
            L"state value"
        };
        auto authorize = account::BuildAccountAuthorizeUri(
            client,
            L"http://127.0.0.1:49152/callback",
            transaction);
        std::wstring authorizeText{ authorize.c_str() };
        Expect(authorizeText.rfind(
            L"https://api.account.example.test/v1/sso/authorize?",
            0) == 0,
            "authorization URL used the wrong account endpoint");
        Expect(authorizeText.find(L"client_id=last-music-desktop") != std::wstring::npos,
            "authorization URL omitted the desktop client id");
        Expect(authorizeText.find(L"redirect_uri=http%3A%2F%2F127.0.0.1%3A49152%2Fcallback") != std::wstring::npos,
            "authorization URL did not escape the loopback redirect");
        Expect(authorizeText.find(L"state=state%20value") != std::wstring::npos,
            "authorization URL did not escape state");
        Expect(authorizeText.find(L"code_challenge=challenge%20value") != std::wstring::npos,
            "authorization URL did not escape the PKCE challenge");
        Expect(authorizeText.find(L"code_challenge_method=S256") != std::wstring::npos,
            "authorization URL omitted the PKCE method");
        Expect(authorizeText.find(L"verifier-must-not-appear") == std::wstring::npos,
            "authorization URL exposed the PKCE verifier");

        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"opaque-session"})") == L"opaque-session",
            "valid account token response was rejected");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"opaque-session==","token_type":"bearer"})")
                == L"opaque-session==",
            "case-insensitive Bearer token type was rejected");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"opaque-session","token_type":"DPoP"})").empty(),
            "non-Bearer account token type was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"opaque-session","token_type":7})").empty(),
            "non-string account token type was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"abc,def"})").empty(),
            "invalid bearer-token punctuation was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"abc=def"})").empty(),
            "bearer-token data after padding was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":7})").empty(),
            "non-string account token was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"line\nbreak"})").empty(),
            "control-bearing account token was accepted");
        Expect(account::ParseAccountBearerSession(
            LR"({"access_token":"two words"})").empty(),
            "whitespace-bearing account token was accepted");
        std::wstring oversizedToken(16 * 1024 + 1, L'a');
        Expect(account::ParseAccountBearerSession(
            winrt::hstring{ L"{\"access_token\":\"" + oversizedToken + L"\"}" }).empty(),
            "oversized account token was accepted");

        auto direct = account::ParseAccountProfile(LR"({
          "id":"owner-1",
          "displayName":"Example User",
          "username":"example",
          "plan":"Test",
          "avatarUrl":"https://cdn.example.test/avatar.png?size=128"
        })");
        Expect(direct.Id == L"owner-1", "direct account profile id was not parsed");
        Expect(direct.DisplayName == L"Example User", "account display name was not parsed");
        Expect(direct.AvatarUrl == L"https://cdn.example.test/avatar.png?size=128",
            "safe account avatar was rejected");

        auto nested = account::ParseAccountProfile(LR"({
          "user":{"id":"owner-2","fullName":"Fallback Name","username":"nested"}
        })");
        Expect(nested.Id == L"owner-2" && nested.DisplayName == L"Fallback Name",
            "nested account profile fallback was not parsed");

        Expect(account::ParseAccountProfile(
            LR"({"id":"owner with spaces"})").Id.empty(),
            "whitespace-bearing owner id was accepted");
        Expect(account::ParseAccountProfile(
            LR"({"id":"owner\n2"})").Id.empty(),
            "control-bearing owner id was accepted");

        std::wstring oversizedName(257, L'n');
        auto bounded = account::ParseAccountProfile(winrt::hstring{
            L"{\"id\":\"owner-3\",\"displayName\":\""
            + oversizedName
            + L"\",\"username\":\"valid\"}"
        });
        Expect(bounded.Id == L"owner-3" && bounded.DisplayName.empty()
            && bounded.Username == L"valid",
            "invalid optional profile text corrupted the profile");

        auto unsafeAvatar = account::ParseAccountProfile(
            LR"({"id":"owner-4","avatarUrl":"file:///C:/private.png"})");
        Expect(unsafeAvatar.AvatarUrl.empty(),
            "unsupported account avatar scheme was accepted");
        unsafeAvatar = account::ParseAccountProfile(
            LR"({"id":"owner-4","avatarUrl":"https://cdn.example.test/avatar.png?access_token=secret"})");
        Expect(unsafeAvatar.AvatarUrl.empty(),
            "credential-bearing account avatar was accepted");
        unsafeAvatar = account::ParseAccountProfile(
            LR"({"id":"owner-4","avatarUrl":"https://cdn.example.test/avatar.png#fragment"})");
        Expect(unsafeAvatar.AvatarUrl.empty(),
            "fragment-bearing account avatar was accepted");
    }

    void TestLoopbackListenerLifecycle()
    {
        {
            account::LoopbackCallback callback;
            Expect(callback.Start(), "loopback callback could not start");
            auto redirectUri = callback.RedirectUri();
            auto result = std::async(std::launch::async, [&callback]
            {
                return callback.WaitForCallback(
                    L"expected-state",
                    std::chrono::steady_clock::now() + std::chrono::seconds(5));
            });

            auto invalidResponse = SendCallbackRequest(
                redirectUri,
                "/wrong?code=ignored&state=expected-state");
            Expect(invalidResponse.find("400 Bad Request") != std::string::npos,
                "invalid callback did not receive a failure page");

            auto target = winrt::to_string(account::BuildConfig::DesktopCallbackPath);
            target += "?code=accepted-code&state=expected-state";
            auto successResponse = SendCallbackRequest(redirectUri, target);
            auto callbackResult = result.get();
            Expect(successResponse.find("200 OK") != std::string::npos,
                "valid callback did not receive a success page");
            Expect(callbackResult.Status == account::LoopbackCallbackStatus::Success
                && callbackResult.Code == L"accepted-code",
                "listener did not accept a valid callback after an invalid probe");
        }

        {
            account::LoopbackCallback callback;
            Expect(callback.Start(), "cancel callback could not start");
            auto redirectUri = callback.RedirectUri();
            auto result = std::async(std::launch::async, [&callback]
            {
                return callback.WaitForCallback(
                    L"cancel-state",
                    std::chrono::steady_clock::now() + std::chrono::seconds(5));
            });
            auto stalledClient = ConnectToLoopback(redirectUri);
            Expect(SendAll(stalledClient, "G"),
                "stalled callback client could not send a partial request");
            auto acceptDeadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(2);
            while (!callback.HasAcceptedClientForTesting()
                && std::chrono::steady_clock::now() < acceptDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Expect(callback.HasAcceptedClientForTesting(),
                "listener did not enter an accepted-client read");
            auto started = std::chrono::steady_clock::now();
            callback.Cancel();
            auto callbackResult = result.get();
            auto elapsed = std::chrono::steady_clock::now() - started;
            ::closesocket(stalledClient);
            Expect(callbackResult.Status == account::LoopbackCallbackStatus::Canceled,
                "canceling a stalled callback did not report cancellation");
            if (elapsed >= std::chrono::seconds(1))
            {
                auto elapsedMilliseconds = std::chrono::duration_cast<
                    std::chrono::milliseconds>(elapsed).count();
                throw std::runtime_error(
                    "canceling a stalled callback took "
                    + std::to_string(elapsedMilliseconds)
                    + " ms");
            }
        }

        {
            account::LoopbackCallback callback;
            Expect(callback.Start(), "stalled-timeout callback could not start");
            auto redirectUri = callback.RedirectUri();
            auto started = std::chrono::steady_clock::now();
            auto result = std::async(std::launch::async, [&callback]
            {
                return callback.WaitForCallback(
                    L"timeout-state",
                    std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
            });
            auto stalledClient = ConnectToLoopback(redirectUri);
            Expect(SendAll(stalledClient, "G"),
                "timeout callback client could not send a partial request");
            auto acceptDeadline = std::chrono::steady_clock::now()
                + std::chrono::seconds(2);
            while (!callback.HasAcceptedClientForTesting()
                && std::chrono::steady_clock::now() < acceptDeadline)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Expect(callback.HasAcceptedClientForTesting(),
                "timeout listener did not enter an accepted-client read");
            auto callbackResult = result.get();
            auto elapsed = std::chrono::steady_clock::now() - started;
            ::closesocket(stalledClient);
            Expect(callbackResult.Status == account::LoopbackCallbackStatus::TimedOut,
                "stalled callback deadline did not report a timeout");
            Expect(elapsed < std::chrono::seconds(1),
                "stalled callback exceeded its overall deadline");
        }

        {
            account::LoopbackCallback callback;
            Expect(callback.Start(), "timeout callback could not start");
            auto result = callback.WaitForCallback(
                L"timeout-state",
                std::chrono::steady_clock::now() + std::chrono::milliseconds(150));
            Expect(result.Status == account::LoopbackCallbackStatus::TimedOut,
                "callback deadline did not report a timeout");
            Expect(callback.Start(), "callback could not restart after timeout");
            callback.Cancel();
        }
    }

    void TestAccountRedirectProtection()
    {
#ifdef _DEBUG
        TestHttpServer target{
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
        };
        auto targetUrl = winrt::to_string(target.Url());
        auto redirectResponse = "HTTP/1.1 302 Found\r\nLocation: "
            + targetUrl
            + "/capture\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        TestHttpServer redirect{ std::move(redirectResponse) };

        account::AccountClient client{ winrt::hstring{ redirect.Url() } };
        bool failed{};
        try
        {
            (void)client.GetMeAsync(L"redirect-secret").get();
        }
        catch (winrt::hresult_error const&)
        {
            failed = true;
        }
        redirect.Join();
        target.Join();

        Expect(failed, "account client accepted an authenticated redirect");
        Expect(redirect.WasCalled(), "redirect test account server was not called");
        auto request = redirect.Request();
        std::transform(request.begin(), request.end(), request.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
        Expect(request.find("authorization: bearer redirect-secret") != std::string::npos,
            "account request did not carry its bearer to the configured origin");
        Expect(!target.WasCalled(),
            "account client followed a redirect to another origin");
#endif
    }

    void TestAccountOriginAndSafeErrors()
    {
        Expect(account::IsTrustedAccountOrigin(L"https://account.example"), "HTTPS account origin was rejected");
#ifdef _DEBUG
        Expect(account::IsTrustedAccountOrigin(L"http://127.0.0.1:8787"), "loopback development origin was rejected");
#else
        Expect(!account::IsTrustedAccountOrigin(L"http://127.0.0.1:8787"), "Release accepted a loopback HTTP account origin");
#endif
        Expect(!account::IsTrustedAccountOrigin(L"http://account.example"), "public HTTP account origin was accepted");
        Expect(!account::IsTrustedAccountOrigin(L"https://account.example/path"), "account origin path was accepted");
        Expect(!account::IsTrustedAccountOrigin(L"https://account.example/?token=secret"), "account origin query was accepted");
        Expect(!account::IsTrustedAccountOrigin(L"https://user:secret@account.example"), "account origin userinfo was accepted");

        // Public builds intentionally leave the account origin blank. If a
        // distributor supplies one, the derived management link must remain
        // HTTPS and target the account page.
        auto managementUrl = account::AccountManagementUrl();
        std::wstring manageUrl{ managementUrl.c_str() };
        if (!manageUrl.empty())
        {
            constexpr std::wstring_view AccountPath{ L"/account" };
            Expect(manageUrl.size() > AccountPath.size()
                && manageUrl.compare(
                    manageUrl.size() - AccountPath.size(),
                    AccountPath.size(),
                    AccountPath.data(),
                    AccountPath.size()) == 0,
                "account management URL did not target the account page");
            auto origin = manageUrl.substr(0, manageUrl.size() - AccountPath.size());
            Expect(account::IsTrustedAccountOrigin(winrt::hstring{ origin }),
                "account management URL used an untrusted origin");
        }

        for (auto status : { 400u, 401u, 403u, 404u, 409u, 429u, 500u })
        {
            auto message = account::SafeAccountErrorMessage(status);
            Expect(!message.empty() && message.size() < 128, "account error was not bounded");
        }
    }

    void TestInlineProfileImageValidation()
    {
        // A one-pixel GIF, the smallest payload that exercises the whole shape.
        winrt::hstring const gif{
            L"data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" };
        Expect(account::IsSafeInlineProfileImage(gif), "a valid inline GIF avatar was rejected");
        Expect(account::IsInlineProfileImageData(gif), "an inline avatar was not recognized as inline");

        // The scheme and media type are case-insensitive per RFC 2397; the
        // base64 payload after the comma is not.
        Expect(account::IsSafeInlineProfileImage(L"DATA:IMAGE/PNG;BASE64,AAAA"),
            "an upper-case data URI header was rejected");
        Expect(account::IsSafeInlineProfileImage(L"data:image/jpeg;base64,/9j/4AAQ"),
            "a JPEG payload using the full base64 alphabet was rejected");
        Expect(account::IsSafeInlineProfileImage(L"data:image/webp;base64,AAA="),
            "a payload with one padding character was rejected");
        Expect(account::IsSafeInlineProfileImage(L"data:image/bmp;base64,AA=="),
            "a payload with two padding characters was rejected");

        // A URL is not inline data, and inline data is not a URL. Neither
        // check may accept the other's input.
        Expect(!account::IsInlineProfileImageData(L"https://cdn.example.test/a.png"),
            "an HTTPS avatar URL was misread as inline data");
        Expect(!account::IsSafeInlineProfileImage(L"https://cdn.example.test/a.png"),
            "an HTTPS avatar URL was accepted as inline data");
        Expect(!account::IsSafeAccountProfileUrl(gif),
            "inline image data was accepted by the URL check");

        // Anything that is not a still image the decoder is expected to
        // handle, or that could name something other than an image.
        Expect(!account::IsSafeInlineProfileImage(L"data:image/svg+xml;base64,AAAA"),
            "an SVG avatar was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:text/html;base64,AAAA"),
            "a non-image media type was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:application/octet-stream;base64,AAAA"),
            "an opaque binary media type was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png,AAAA"),
            "a non-base64 data URI was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,"),
            "an empty payload was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64"),
            "a data URI with no payload separator was accepted");

        // A malformed payload must be rejected at the boundary rather than
        // stored and replayed into the image decoder on every launch.
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,AA*A"),
            "a payload outside the base64 alphabet was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,AA-_"),
            "a base64url payload was accepted for a standard base64 field");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,AAAAA"),
            "a payload of non-multiple-of-four length was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,A==="),
            "an over-padded payload was accepted");
        Expect(!account::IsSafeInlineProfileImage(L"data:image/png;base64,===="),
            "a payload of pure padding was accepted");

        // An unbounded picture would be held in memory and written to the
        // account database once per signed-in account.
        std::wstring oversized{ L"data:image/png;base64," };
        oversized.append(1'500'004, L'A');
        Expect(!account::IsSafeInlineProfileImage(winrt::hstring{ oversized }),
            "an oversized inline avatar was accepted");
    }

    void TestProfileIdentitySelection()
    {
        account::AccountProfile full;
        full.Id = L"owner-1";
        full.Username = L"debashis";
        full.DisplayName = L"  Debashis  ";
        full.AvatarUrl = L"https://cdn.example.test/avatar.png";
        full.PlanLabel = L" Premium ";

        // Only account mode with a live-or-cached session hands the shell over
        // to the server-owned profile.
        auto connected = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::Validated,
            full,
            L"Typed Name");
        Expect(connected.Source == account::ProfileIdentitySource::Account,
            "a validated account session did not take over the identity");
        Expect(connected.Name == L"Debashis", "the account display name was not trimmed");
        Expect(connected.AvatarUrl == full.AvatarUrl, "the account avatar was dropped");
        Expect(connected.PlanLabel == L"Premium", "the plan label was not trimmed");

        auto offline = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::Offline,
            full,
            L"Typed Name");
        Expect(offline.Source == account::ProfileIdentitySource::Account,
            "an offline session fell back to the manual identity");
        Expect(offline.Name == L"Debashis", "the cached profile name was not used while offline");

        // The manual name is only ignored, never consumed, so every other
        // combination restores exactly what the user typed.
        for (auto mode : { account::RemoteAccessMode::LocalOnly, account::RemoteAccessMode::ApiKey })
        {
            auto manual = account::ChooseProfileIdentity(
                mode,
                account::AccountSessionStatus::Validated,
                full,
                L"  Typed Name  ");
            Expect(manual.Source == account::ProfileIdentitySource::Manual,
                "a non-account mode adopted the account identity");
            Expect(manual.Name == L"Typed Name", "the manual name was not trimmed");
            Expect(manual.AvatarUrl.empty(), "a non-account mode kept the account avatar");
            Expect(manual.PlanLabel.empty(), "a non-account mode kept the account plan label");
        }

        auto signedOut = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::SignedOut,
            account::AccountProfile{},
            L"Typed Name");
        Expect(signedOut.Source == account::ProfileIdentitySource::Manual,
            "account mode without a session claimed an account identity");
        Expect(signedOut.Name == L"Typed Name", "signing out discarded the manual name");

        auto signingIn = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::SigningIn,
            full,
            L"Typed Name");
        Expect(signingIn.Source == account::ProfileIdentitySource::Manual,
            "an in-flight sign-in published the profile before it was validated");

        // The server treats display name, username and plan as optional, so
        // each one degrades on its own rather than voiding the whole identity.
        account::AccountProfile usernameOnly;
        usernameOnly.Id = L"owner-2";
        usernameOnly.Username = L"listener42";
        auto byUsername = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::Validated,
            usernameOnly,
            L"Typed Name");
        Expect(byUsername.Source == account::ProfileIdentitySource::Account,
            "a profile with no display name lost its account identity");
        Expect(byUsername.Name == L"listener42", "the username was not used as the display fallback");
        Expect(byUsername.PlanLabel.empty(), "a missing plan label was invented");

        account::AccountProfile nameless;
        nameless.Id = L"owner-3";
        auto byManual = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::Validated,
            nameless,
            L"Typed Name");
        Expect(byManual.Name == L"Typed Name",
            "a nameless profile did not fall back to the manual name");
        Expect(byManual.Source == account::ProfileIdentitySource::Account,
            "a nameless profile stopped being an account identity");

        auto unnamed = account::ChooseProfileIdentity(
            account::RemoteAccessMode::Account,
            account::AccountSessionStatus::Validated,
            nameless,
            L"   ");
        Expect(unnamed.Name == account::kDefaultListenerName,
            "an identity with no name anywhere did not fall back to Listener");

        auto blankManual = account::ChooseProfileIdentity(
            account::RemoteAccessMode::LocalOnly,
            account::AccountSessionStatus::SignedOut,
            account::AccountProfile{},
            L" \t ");
        Expect(blankManual.Name == account::kDefaultListenerName,
            "a whitespace-only manual name did not fall back to Listener");
    }

    void TestSyncableRemoteSources()
    {
        // Catalog rows are resolved to a playable source at playback time, so
        // sync must carry their durable references instead of shrinking a
        // library curated on another client.
        Expect(account::IsSyncableRemoteSource(
            L"remote", L"https://catalog.example.test/tracks/1234567891"),
            "a catalog source was dropped from sync");
        Expect(account::IsSyncableRemoteSource(L"remote", L"https://example.test/track/42"),
            "an ordinary HTTPS remote source was dropped from sync");
        Expect(account::IsSyncableRemoteSource(L"", L"https://example.test/track/42"),
            "a source with no declared kind was dropped from sync");
        Expect(account::IsSyncableRemoteSource(L"remote", L"http://127.0.0.1:4527/v1/stream/42"),
            "a loopback development source was dropped from sync");

        // A local row names a path on the machine that scanned it.
        Expect(!account::IsSyncableRemoteSource(L"local", L"https://example.test/track/42"),
            "a local-kind row was accepted for sync");
        Expect(!account::IsSyncableRemoteSource(L"LOCAL", L"https://example.test/track/42"),
            "source kind matching was case sensitive");

        // Cached rows are replayed long after the sync that stored them, so a
        // URL that only works while a credential or signature is live is useless.
        Expect(!account::IsSyncableRemoteSource(
            L"remote", L"https://example.test/track/42?access_token=secret"),
            "a credential-bearing source was accepted for sync");
        Expect(!account::IsSyncableRemoteSource(
            L"remote", L"https://example.test/track/42?expires=1750000000&signature=abc"),
            "a signed-delivery source was accepted for sync");
        Expect(!account::IsSyncableRemoteSource(L"remote", L"https://user:secret@example.test/track/42"),
            "a source with embedded userinfo was accepted for sync");
        Expect(!account::IsSyncableRemoteSource(L"remote", L"http://example.test/track/42"),
            "a public plaintext HTTP source was accepted for sync");
        Expect(!account::IsSyncableRemoteSource(L"remote", L""),
            "an empty source URL was accepted for sync");
    }

    // Representative /v1/music/catalog/discovery response. The parser reads a
    // service-owned shape, so this fixture catches contract drift at build time
    // instead of producing empty shelves on a user's machine.
    constexpr wchar_t kDiscoveryFixture[] = LR"({
      "storefront": "in",
      "storefrontName": "India",
      "shelves": [
        {
          "id": "top-songs",
          "title": "Top Songs",
          "resourceType": "song",
          "chart": { "kind": "global", "id": "top-songs", "resourceType": "song" },
          "items": [
            {
              "id": "example-catalog:1811023667",
              "catalogId": "1811023667",
              "resourceType": "song",
              "provider": "example-catalog",
              "title": "Sample Song",
              "artistName": "Sample Artist",
              "albumName": "Sample Album",
              "albumCatalogId": "1811023660",
              "artistCatalogId": "1440817000",
              "durationMs": 215000,
              "artworkUrl": "https://cdn.example.test/artwork/song.jpg",
              "sourceUrl": "https://catalog.example.test/albums/sample-album/tracks/1811023667",
              "releaseDate": "2026-01-09",
              "genreNames": ["Pop", "Indian Pop"]
            },
            {
              "catalogId": "9",
              "resourceType": "song",
              "title": "",
              "artistName": "Untitled"
            }
          ]
        },
        {
          "id": "top-playlists",
          "title": "Top Playlists",
          "resourceType": "playlist",
          "chart": { "kind": "global", "id": "top-playlists", "resourceType": "playlist" },
          "items": [
            {
              "catalogId": "pl.abc123",
              "resourceType": "playlist",
              "name": "Sample Playlist",
              "curatorName": "Example Curator",
              "artworkUrl": "https://cdn.example.test/artwork/playlist.jpg",
              "sourceUrl": "https://catalog.example.test/playlists/pl.abc123",
              "description": "A sample playlist."
            }
          ]
        },
        {
          "id": "empty-shelf",
          "title": "Nothing Here",
          "resourceType": "album",
          "items": []
        }
      ],
      "charts": [
        { "kind": "global", "id": "top-songs", "name": "India", "title": "Top Songs in India", "subtitle": "Current music region", "resourceType": "song" },
        { "kind": "genre", "id": "14", "name": "Pop", "title": "Top Pop Songs", "subtitle": "Genre chart", "resourceType": "song" },
        { "kind": "city", "id": "city-mumbai", "name": "Mumbai", "title": "Top Songs in Mumbai", "subtitle": "Mumbai city chart", "resourceType": "song" },
        { "kind": "nearby", "id": "unsafe", "name": "Ignored", "title": "Ignored", "resourceType": "song" }
      ],
      "cityChartGroups": {
        "indian": {
          "charts": [
            { "storefront": "in", "storefrontName": "India", "kind": "city", "id": "city-mumbai", "name": "Mumbai", "title": "Top Songs in Mumbai", "resourceType": "song", "artworkUrl": "https://cdn.example.test/mumbai.jpg" },
            { "kind": "city", "id": "city-pune", "name": "Pune", "title": "Top Songs in Pune", "resourceType": "song" }
          ],
          "partial": false
        },
        "international": {
          "charts": [
            { "storefront": "gb", "storefrontName": "United Kingdom", "kind": "city", "id": "city-london", "name": "London", "title": "Top Songs in London", "resourceType": "song" }
          ],
          "partial": true
        }
      },
      "moodActivityShelf": {
        "id": "moods-activities",
        "title": "Moods & Activities",
        "resourceType": "playlist",
        "items": [
          { "catalogId": "mood-focus", "resourceType": "playlist", "name": "Pure Focus", "curatorName": "Apple Music" }
        ]
      },
      "stale": true,
      "fetchedAt": "2026-08-01T00:00:00.000Z"
    })";

    void TestCatalogDiscoveryParsing()
    {
        auto discovery = account::ParseCatalogDiscovery(kDiscoveryFixture, L"in");
        Expect(discovery.Storefront == L"in", "discovery storefront was not read");
        Expect(discovery.StorefrontName == L"India", "discovery storefront name was not read");
        Expect(discovery.Stale, "discovery stale state was not read");
        Expect(discovery.FetchedAt == L"2026-08-01T00:00:00.000Z", "discovery fetchedAt was not read");
        // The empty shelf is dropped: an titled shelf with no tiles is noise.
        Expect(discovery.Shelves.size() == 2, "discovery shelves were not parsed as expected");

        auto const& songs = discovery.Shelves[0];
        Expect(songs.Id == L"top-songs", "song shelf id was not read");
        Expect(songs.Title == L"Top Songs", "song shelf title was not read");
        Expect(songs.ItemType == account::CatalogResourceType::Song, "song shelf type was not read");
        Expect(songs.Chart.has_value(), "song shelf chart identity was not read");
        Expect(songs.Chart->Kind == account::CatalogChartKind::Global, "song shelf chart kind was not read");
        Expect(songs.Chart->Id == L"top-songs", "song shelf chart id was not read");
        // The second item has no title and cannot be rendered.
        Expect(songs.Items.size() == 1, "untitled shelf items were not dropped");

        auto const& song = songs.Items[0];
        Expect(song.Type == account::CatalogResourceType::Song, "song item type was not read");
        Expect(song.CatalogId == L"1811023667", "song catalog id was not read");
        Expect(song.RemoteId == L"example-catalog:1811023667", "stable song remote id was not read");
        Expect(song.Title == L"Sample Song", "song title was not read");
        Expect(song.Subtitle == L"Sample Artist", "song artist was not read");
        Expect(song.AlbumName == L"Sample Album", "song album name was not read");
        Expect(song.AlbumCatalogId == L"1811023660", "song album catalog id was not read");
        Expect(song.ArtistCatalogId == L"1440817000", "song artist catalog id was not read");
        Expect(song.DurationMs == 215000, "song duration was not read");
        Expect(song.Provider == L"example-catalog", "song provider was not read");
        // The source URL is what makes tap-to-play work through the existing
        // resolve path, so losing it silently breaks playback.
        Expect(song.SourceUrl == L"https://catalog.example.test/albums/sample-album/tracks/1811023667",
            "song source URL was not read");
        Expect(song.GenreNames.size() == 2 && song.GenreNames[0] == L"Pop", "song genres were not read");

        auto const& playlists = discovery.Shelves[1];
        Expect(playlists.ItemType == account::CatalogResourceType::Playlist, "playlist shelf type was not read");
        Expect(playlists.Items.size() == 1, "playlist shelf items were not parsed");
        // Playlists carry `name` and `curatorName` where songs carry
        // `title` and `artistName`; both have to land in the same fields.
        Expect(playlists.Items[0].Title == L"Sample Playlist", "playlist name was not read");
        Expect(playlists.Items[0].Subtitle == L"Example Curator", "playlist curator was not read");
        Expect(playlists.Items[0].Description == L"A sample playlist.", "playlist description was not read");

        Expect(discovery.Charts.size() == 3, "valid global, genre, and city charts were not retained");
        Expect(discovery.Charts[0].Ref.Kind == account::CatalogChartKind::Global, "global chart kind was not parsed");
        Expect(discovery.Charts[1].Ref.Kind == account::CatalogChartKind::Genre, "genre chart kind was not parsed");
        Expect(discovery.Charts[2].Ref.Kind == account::CatalogChartKind::City, "city chart kind was not parsed");
        Expect(discovery.CityChartGroups.has_value(), "grouped city charts were not parsed");
        Expect(discovery.CityChartGroups->Indian.Charts.size() == 1,
            "grouped city chart without an explicit storefront was not dropped");
        Expect(discovery.CityChartGroups->International.Charts.size() == 1,
            "international city chart was not parsed");
        Expect(discovery.CityChartGroups->International.Charts[0].Ref.Storefront == L"gb",
            "international city storefront was lost");
        Expect(discovery.CityChartGroups->International.Partial,
            "partial international city state was not parsed");
        Expect(discovery.MoodActivityShelf.has_value(), "mood and activity shelf was not parsed");
        Expect(discovery.MoodActivityShelf->Items.size() == 1,
            "mood and activity shelf items were not parsed");
    }

    void TestCatalogChartAndDetailParsing()
    {
        account::CatalogChartRef topSongs{
            L"in", account::CatalogChartKind::Global, L"top-songs", account::CatalogResourceType::Song
        };
        auto page = account::ParseCatalogChartPage(LR"({
          "storefront": "in",
          "type": "songs",
          "id": "global:top-songs",
          "title": "Top Songs in India",
          "resourceType": "song",
          "items": [
            { "catalogId": "1", "resourceType": "song", "title": "One", "artistName": "A" },
            { "catalogId": "2", "resourceType": "song", "title": "Two", "artistName": "B" }
          ],
          "nextOffset": 50
        })", topSongs);
        Expect(page.Title == L"Top Songs in India", "chart title was not read");
        Expect(page.ItemType == account::CatalogResourceType::Song, "chart resource type was not read");
        Expect(page.Items.size() == 2, "chart items were not parsed");
        Expect(page.HasNextOffset && page.NextOffset == 50, "chart next offset was not read");

        auto mismatchedGlobal = account::ParseCatalogChartPage(LR"({
          "storefront": "in", "type": "albums", "id": "global:top-albums", "items": []
        })", topSongs);
        Expect(!mismatchedGlobal.Descriptor.has_value() && mismatchedGlobal.Items.empty(),
            "mismatched global chart response was accepted");

        auto invalidResponseStorefront = account::ParseCatalogChartPage(LR"({
          "storefront": "india", "type": "songs", "id": "global:top-songs", "items": []
        })", topSongs);
        Expect(!invalidResponseStorefront.Descriptor.has_value() && invalidResponseStorefront.Items.empty(),
            "invalid response storefront was accepted");

        // A null nextOffset is the service saying this is the last page.
        auto lastPage = account::ParseCatalogChartPage(LR"({
          "storefront": "in", "type": "songs", "resourceType": "song",
          "items": [], "nextOffset": null
        })", topSongs);
        Expect(!lastPage.HasNextOffset, "a null next offset was treated as another page");

        auto album = account::ParseCatalogResourceDetail(LR"({
          "storefront": "in",
          "album": {
            "catalogId": "1811023660",
            "resourceType": "album",
            "name": "Sample Album",
            "artistName": "Sample Artist",
            "trackCount": 2,
            "editorialNotes": "Notes about the album."
          },
          "tracks": [
            { "catalogId": "1", "resourceType": "song", "title": "One", "artistName": "Sample Artist" },
            { "catalogId": "2", "resourceType": "song", "title": "Two", "artistName": "Sample Artist" }
          ]
        })", L"albums");
        Expect(album.Resource.Title == L"Sample Album", "album name was not read");
        Expect(album.Resource.Type == account::CatalogResourceType::Album, "album type was not read");
        Expect(album.Resource.TrackCount == 2, "album track count was not read");
        Expect(album.Resource.Description == L"Notes about the album.", "album editorial notes were not read");
        Expect(album.Tracks.size() == 2, "album tracks were not parsed");

        auto playlist = account::ParseCatalogResourceDetail(LR"({
          "storefront": "in",
          "playlist": { "catalogId": "pl.1", "resourceType": "playlist", "name": "Sample Playlist" },
          "tracks": [ { "catalogId": "1", "resourceType": "song", "title": "One" } ]
        })", L"playlists");
        Expect(playlist.Resource.Title == L"Sample Playlist", "playlist name was not read");
        Expect(playlist.Tracks.size() == 1, "playlist tracks were not parsed");

        auto storefronts = account::ParseCatalogStorefronts(LR"({
          "storefronts": [
            { "code": "in", "name": "India" },
            { "code": "us", "name": "" },
            { "name": "No Code" }
          ]
        })");
        Expect(storefronts.size() == 2, "storefronts without a code were not dropped");
        Expect(storefronts[0].Code == L"in" && storefronts[0].Name == L"India", "storefront was not read");
        // A code with no name still has to be selectable in the picker.
        Expect(storefronts[1].Name == L"us", "a nameless storefront did not fall back to its code");
    }

    void TestCatalogTrustBoundaryValidation()
    {
        using account::CatalogChartKind;
        using account::CatalogChartRef;
        using account::CatalogResourceType;

        auto validGenre = CatalogChartRef{ L"in", CatalogChartKind::Genre, L"14", CatalogResourceType::Song };
        Expect(account::IsValidCatalogChartRef(validGenre), "valid genre chart was rejected");
        Expect(!account::IsValidCatalogChartRef({ L"india", CatalogChartKind::Genre, L"14", CatalogResourceType::Song }),
            "invalid storefront was accepted");
        Expect(!account::IsValidCatalogChartRef({ L"in", CatalogChartKind::Genre, L"14/../etc", CatalogResourceType::Song }),
            "unsafe chart id was accepted");
        Expect(!account::IsValidCatalogChartRef({ L"in", CatalogChartKind::Global, L"top-artists", CatalogResourceType::Artist }),
            "artist chart was accepted");
        Expect(!account::IsValidCatalogChartRef({ L"in", CatalogChartKind::City, L"city-mumbai", CatalogResourceType::Album }),
            "non-song city chart was accepted");
        Expect(!account::IsValidCatalogChartRef({ L"in", CatalogChartKind::Genre, L"14", CatalogResourceType::Playlist }),
            "non-song genre chart was accepted");
        Expect(!account::IsValidCatalogChartRef({ L"in", CatalogChartKind::Global, L"top-hits", CatalogResourceType::Song }),
            "non-canonical global chart was accepted");

        auto mismatch = account::ParseCatalogDiscovery(kDiscoveryFixture, L"us");
        Expect(mismatch.Storefront.empty() && mismatch.Shelves.empty(),
            "discovery from a different storefront was accepted");

        auto unsafeArtwork = account::ParseCatalogDiscovery(LR"json({
          "storefront":"in",
          "shelves":[{"id":"top-songs","title":"Top Songs","resourceType":"song","items":[
            {"catalogId":"1","resourceType":"song","title":"One","artworkUrl":"javascript:alert(1)"}
          ]}],
          "charts":[{"kind":"genre","id":"14","name":"Pop","title":"Pop","resourceType":"song","artworkUrl":"/relative.jpg"}]
        })json", L"in");
        Expect(unsafeArtwork.Shelves[0].Items[0].ArtworkUrl.empty(), "unsafe item artwork URL was retained");
        Expect(unsafeArtwork.Charts[0].ArtworkUrl.empty(), "unsafe chart artwork URL was retained");

        auto cityRequest = CatalogChartRef{ L"in", CatalogChartKind::City, L"city-mumbai", CatalogResourceType::Song };
        auto wrongChart = account::ParseCatalogChartPage(LR"({
          "storefront":"in",
          "descriptor":{"storefront":"in","kind":"city","id":"city-pune","name":"Pune","title":"Top Songs in Pune","resourceType":"song"},
          "items":[{"catalogId":"1","resourceType":"song","title":"One"}]
        })", cityRequest);
        Expect(!wrongChart.Descriptor.has_value() && wrongChart.Items.empty(),
            "response for a different city chart was accepted");

        auto wrongStorefront = account::ParseCatalogChartPage(LR"({
          "storefront":"gb",
          "descriptor":{"storefront":"in","kind":"city","id":"city-mumbai","name":"Mumbai","title":"Top Songs in Mumbai","resourceType":"song"},
          "items":[{"catalogId":"1","resourceType":"song","title":"One"}]
        })", cityRequest);
        Expect(!wrongStorefront.Descriptor.has_value() && wrongStorefront.Items.empty(),
            "chart response from a different storefront was accepted");

        auto legacy = account::ParseCatalogDiscovery(LR"({
          "storefront":"in",
          "shelves":[{"id":"top-albums","title":"Top Albums","resourceType":"album","items":[
            {"catalogId":"a1","resourceType":"album","name":"Album"}
          ]}]
        })", L"in");
        Expect(legacy.Shelves.size() == 1 && legacy.Shelves[0].Chart.has_value(),
            "legacy top shelf did not infer its global chart");
        Expect(legacy.Shelves[0].Chart->Id == L"top-albums", "legacy chart inference used the wrong id");
    }

    void TestCatalogChartRequestConstruction()
    {
#ifdef _DEBUG
        auto response = std::string{
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}"
        };
        auto requestPath = [&](account::CatalogChartRequest const& request)
        {
            TestHttpServer server{ response };
            account::AccountClient client{ winrt::hstring{ server.Url() } };
            (void)client.GetCatalogChartAsync(L"catalog-token", request).get();
            server.Join();
            return server.Request();
        };

        {
            TestHttpServer server{ response };
            account::AccountClient client{ winrt::hstring{ server.Url() } };
            (void)client.GetCatalogDiscoveryAsync(L"catalog-token", L"IN").get();
            server.Join();
            auto discovery = server.Request();
            Expect(discovery.find("GET /v1/music/catalog/discovery?storefront=in&includeCityChartGroups=true ") != std::string::npos,
                "discovery request did not normalize storefront or request grouped cities");
        }

        auto global = requestPath({
            { L"in", account::CatalogChartKind::Global, L"top-albums", account::CatalogResourceType::Album },
            25,
            10
        });
        Expect(global.find("GET /v1/music/catalog/charts?storefront=in&type=albums&limit=25&offset=10 ") != std::string::npos,
            "global chart request URL was incorrect");

        auto city = requestPath({
            { L"gb", account::CatalogChartKind::City, L"city-london", account::CatalogResourceType::Song },
            50,
            0
        });
        Expect(city.find("&cityId=city-london ") != std::string::npos,
            "city chart request did not carry the city id");

        auto genre = requestPath({
            { L"in", account::CatalogChartKind::Genre, L"14", account::CatalogResourceType::Song },
            500,
            -3
        });
        Expect(genre.find("&limit=100&offset=0&genreId=14 ") != std::string::npos,
            "genre chart request did not clamp paging or carry the genre id");
#endif
    }

    void TestCatalogPresentationRules()
    {
        auto discovery = account::ParseCatalogDiscovery(kDiscoveryFixture, L"in");
        auto sections = account::BuildCatalogChartSections(discovery);
        Expect(sections.size() == 2, "catalog charts did not split into explore and city sections");
        Expect(sections[0].Id == L"explore" && sections[0].Charts.size() == 2,
            "explore charts contained the wrong chart kinds");
        Expect(sections[1].Id == L"cities" && sections[1].Charts.size() == 2,
            "grouped city charts did not replace the legacy city list");
        Expect(sections[1].Charts[0].Ref.Storefront == L"in"
            && sections[1].Charts[1].Ref.Storefront == L"gb",
            "city charts were not ordered home-region first");
        Expect(sections[1].Partial, "partial city group state was not carried to presentation");

        auto legacy = discovery;
        legacy.CityChartGroups.reset();
        auto legacySections = account::BuildCatalogChartSections(legacy);
        Expect(legacySections.size() == 2 && legacySections[1].Charts.size() == 1,
            "legacy city chart fallback was not preserved");

        Expect(account::CatalogChartPreviewCount(25) == 18, "home chart preview exceeded 18 cards");
        Expect(account::CatalogChartPreviewCount(3) == 3, "short chart preview was truncated");
        Expect(account::CatalogMoodPreviewCount(25) == 12, "mood preview exceeded 12 items");
        Expect(!account::CatalogChartSectionShowsSeeAll(4), "four chart cards incorrectly showed See all");
        Expect(account::CatalogChartSectionShowsSeeAll(5), "five chart cards did not show See all");

        account::CatalogResourceDetail albumDetail;
        albumDetail.Resource.Type = account::CatalogResourceType::Album;
        albumDetail.Resource.ArtworkUrl = L"https://is1-ssl.mzstatic.com/album/1200x1200bb.jpg";
        account::CatalogItem missingArtwork;
        account::CatalogItem explicitArtwork;
        explicitArtwork.ArtworkUrl = L"https://artist.example.test/single.jpg";
        albumDetail.Tracks = { missingArtwork, explicitArtwork };
        account::ApplyCatalogDetailTrackArtwork(albumDetail);
        Expect(albumDetail.Tracks[0].ArtworkUrl == albumDetail.Resource.ArtworkUrl,
            "an album track without artwork did not inherit the album cover");
        Expect(albumDetail.Tracks[1].ArtworkUrl == albumDetail.Resource.ArtworkUrl,
            "provider track artwork overrode the authoritative album cover");

        account::CatalogResourceDetail playlistDetail;
        playlistDetail.Resource.Type = account::CatalogResourceType::Playlist;
        playlistDetail.Resource.ArtworkUrl = L"https://images.example.test/playlist.jpg";
        playlistDetail.Tracks = { missingArtwork };
        account::ApplyCatalogDetailTrackArtwork(playlistDetail);
        Expect(playlistDetail.Tracks[0].ArtworkUrl.empty(),
            "playlist artwork incorrectly replaced an individual track cover");
    }

    void TestCatalogParsingSurvivesBadInput()
    {
        // The payload crosses a service boundary, so malformed input is an
        // expected runtime condition, not a programming error. Every one of
        // these must yield an empty result rather than throw.
        wchar_t const* malformed[] = {
            L"",
            L"not json at all",
            L"{",
            L"[]",
            L"null",
            LR"({"shelves": "not an array"})",
            LR"({"shelves": [null, 42, "text"]})",
            LR"({"shelves": [{"id": 7, "title": true, "items": [{"title": 5}]}]})",
            LR"({"storefronts": {"code": "in"}})",
            LR"({"items": [{"durationMs": "long"}], "nextOffset": "soon"})",
        };
        for (auto json : malformed)
        {
            Expect(account::ParseCatalogDiscovery(json).Shelves.empty(),
                "malformed discovery input produced shelves");
            Expect(account::ParseCatalogStorefronts(json).empty(),
                "malformed storefront input produced storefronts");
            auto page = account::ParseCatalogChartPage(
                json,
                { L"in", account::CatalogChartKind::Global, L"top-songs", account::CatalogResourceType::Song });
            Expect(page.Items.empty() && !page.HasNextOffset,
                "malformed chart input produced items");
            Expect(account::ParseCatalogResourceDetail(json, L"albums").Tracks.empty(),
                "malformed detail input produced tracks");
        }
    }

    void TestOwnerScopedAccountSchema()
    {
        sqlite3* raw{};
        Expect(sqlite3_open(":memory:", &raw) == SQLITE_OK, "unable to open in-memory SQLite database");
        std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database{ raw, sqlite3_close };

        auto exec = [&](char const* sql)
        {
            char* error{};
            auto rc = sqlite3_exec(database.get(), sql, nullptr, nullptr, &error);
            std::string message = error ? error : "SQLite statement failed";
            sqlite3_free(error);
            if (rc != SQLITE_OK)
            {
                throw std::runtime_error(message);
            }
        };
        auto scalar = [&](char const* sql)
        {
            sqlite3_stmt* rawStatement{};
            Expect(sqlite3_prepare_v2(database.get(), sql, -1, &rawStatement, nullptr) == SQLITE_OK, "unable to prepare SQLite scalar");
            std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement{ rawStatement, sqlite3_finalize };
            Expect(sqlite3_step(statement.get()) == SQLITE_ROW, "SQLite scalar returned no row");
            return sqlite3_column_int(statement.get(), 0);
        };

        exec("PRAGMA foreign_keys=ON;");
        exec(
            "CREATE TABLE Tracks ("
            "Id INTEGER PRIMARY KEY AUTOINCREMENT, SourceKey TEXT, SourceKind TEXT, Provider TEXT, SourceUrl TEXT, FilePath TEXT, "
            "Title TEXT, Artist TEXT, Album TEXT, Genre TEXT, DurationSeconds REAL, ArtworkUrl TEXT, DateAddedSortKey REAL, "
            "DateAddedText TEXT, DurationText TEXT, PlayCount INTEGER, LastPlayedOrder INTEGER, IsLiked INTEGER, IsActive INTEGER, "
            "UpdatedAt INTEGER, LastPlayed TEXT, RemoteId TEXT);"
            "INSERT INTO Tracks (SourceKey,SourceKind,Provider,SourceUrl,FilePath,Title,IsActive,RemoteId) VALUES "
            "('local|one','local','','','C:/Music/local.flac','Local song',1,''),"
            "('remote|api|a','remote','api','https://music.example/a','https://stream.example/a?media_token=secret','API duplicate',1,'remote-a'),"
            "('account|owner-a|old','remote','account','https://music.example/old','','Leaked account row',1,'old');");
        exec("CREATE TABLE Albums (ArtworkUrl TEXT); CREATE TABLE Playlists (ArtworkUrl TEXT);");
        exec(account::DatabaseAccountSchema::CreateTablesSql);
        exec("UPDATE Tracks SET ArtworkUrl='https://images.example/a?signature=secret' WHERE SourceKey='remote|api|a';");
        exec(account::DatabaseAccountSchema::RemoveLegacyGenericAccountTracksSql);
        Expect(scalar("SELECT COUNT(*) FROM Tracks WHERE Provider='account';") == 0,
            "pre-cutover generic account track was not removed");
        exec(account::DatabaseAccountSchema::RemoveTransientRemoteUrlsSql);
        Expect(scalar("SELECT COUNT(*) FROM Tracks WHERE SourceKey='remote|api|a' AND FilePath='' AND ArtworkUrl='';") == 1,
            "transient remote URLs were not removed from durable track state");
        exec(account::DatabaseAccountSchema::CreateEffectiveTracksViewSql);
        exec(
            "INSERT INTO AccountProfiles (AccountId,DisplayName) VALUES ('owner-a','A'),('owner-b','B');"
            "INSERT INTO AccountTracks (AccountId,RemoteId,SourceUrl,Title,IsLiked,PlayCount) VALUES "
            "('owner-a','remote-a','https://music.example/a','Account A song',1,4),"
            "('owner-b','remote-b','https://music.example/b','Account B song',0,2);"
            "INSERT INTO PlaybackEvents (EventId,AccountId,RemoteId,TrackJson,PlayedAtUtc) VALUES "
            "('event-a','owner-a','remote-a','{}','2026-08-01T00:00:00Z'),"
            "('event-b','owner-b','remote-b','{}','2026-08-01T00:00:01Z');"
            "INSERT INTO PendingLikes (AccountId,RemoteId,DesiredState,TrackJson,UpdatedAtUtc) VALUES "
            "('owner-a','shared-track',1,'{}','2026-08-01T00:00:00Z'),"
            "('owner-b','shared-track',0,'{}','2026-08-01T00:00:01Z');"
            "INSERT INTO AccountSyncState (AccountId,LegacyHistoryImportState) VALUES "
            "('owner-a','accepted'),('owner-b','declined');");

        exec("INSERT INTO ActiveAccountContext (SingletonId,RemoteMode,AccountId) VALUES (1,'Account','owner-a');");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account A song';") == 1,
            "active owner A track was not visible");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account B song';") == 0,
            "owner B track leaked into owner A library");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='API duplicate';") == 0,
            "account track did not win legacy duplicate resolution");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND FilePath<>'';") == 0,
            "account cache exposed a persisted media path");

        exec("UPDATE ActiveAccountContext SET RemoteMode='Account', AccountId='owner-b' WHERE SingletonId=1;");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account A song';") == 0,
            "owner A track leaked into owner B library");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account B song';") == 1,
            "active owner B track was not visible");
        Expect(scalar("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-a';") == 1,
            "switching owners removed owner A playback outbox");
        Expect(scalar("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-b';") == 1,
            "switching owners removed owner B playback outbox");
        Expect(scalar("SELECT COUNT(*) FROM PendingLikes WHERE AccountId='owner-a' AND DesiredState=1;") == 1,
            "owner A pending like was not isolated");
        Expect(scalar("SELECT COUNT(*) FROM PendingLikes WHERE AccountId='owner-b' AND DesiredState=0;") == 1,
            "owner B pending like was not isolated");
        Expect(scalar("SELECT COUNT(*) FROM AccountSyncState WHERE AccountId='owner-a' AND LegacyHistoryImportState='accepted';") == 1,
            "owner A legacy import state was not isolated");
        Expect(scalar("SELECT COUNT(*) FROM AccountSyncState WHERE AccountId='owner-b' AND LegacyHistoryImportState='declined';") == 1,
            "owner B legacy import state was not isolated");


        exec("UPDATE ActiveAccountContext SET RemoteMode='LocalOnly', AccountId='' WHERE SingletonId=1;");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE SourceKind='remote';") == 0,
            "local-only mode exposed remote tracks");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='Local song';") == 1,
            "local-only mode hid local tracks");

        exec("UPDATE ActiveAccountContext SET RemoteMode='ApiKey', AccountId='' WHERE SingletonId=1;");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='API duplicate';") == 1,
            "API-key mode did not expose API-key tracks");
        Expect(scalar("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account';") == 0,
            "API-key mode exposed account tracks");

        exec("DELETE FROM PlaybackEvents WHERE AccountId='owner-a' AND EventId='event-a';");
        Expect(scalar("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-a';") == 0,
            "acknowledged owner A playback event remained queued");
        Expect(scalar("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-b';") == 1,
            "owner A acknowledgement removed owner B event");

        exec("DELETE FROM AccountProfiles WHERE AccountId='owner-a';");
        Expect(scalar("SELECT COUNT(*) FROM AccountTracks WHERE AccountId='owner-a';") == 0,
            "clearing owner A profile did not cascade its cache");
        Expect(scalar("SELECT COUNT(*) FROM AccountTracks WHERE AccountId='owner-b';") == 1,
            "clearing owner A profile removed owner B cache");
        Expect(scalar("SELECT COUNT(*) FROM AccountSyncState WHERE AccountId='owner-a';") == 0,
            "clearing owner A profile did not cascade its import state");
        Expect(scalar("SELECT COUNT(*) FROM AccountSyncState WHERE AccountId='owner-b';") == 1,
            "clearing owner A profile removed owner B import state");
    }
    void TestPlaybackHistoryQualification()
    {
        account::PlaybackHistoryQualifier qualifier;
        qualifier.Reset(L"owner-a|track-a");
        Expect(!qualifier.Observe(L"owner-a|track-a", true, 1000), "first playback sample counted opening time");
        for (std::uint64_t tick = 2000; tick <= 8000; tick += 1000)
        {
            Expect(!qualifier.Observe(L"owner-a|track-a", true, tick), "history qualified before eight played seconds");
        }
        Expect(qualifier.Observe(L"owner-a|track-a", true, 9000), "history did not qualify at eight played seconds");
        qualifier.MarkCompleted();
        Expect(!qualifier.Observe(L"owner-a|track-a", true, 10000), "completed playback qualified twice");

        qualifier.Reset(L"owner-a|track-b");
        Expect(!qualifier.Observe(L"owner-a|track-b", true, 1000), "first sample unexpectedly qualified playback");
        Expect(!qualifier.Observe(L"owner-a|track-b", true, 5000), "stalled timer sample exceeded its cap");
        Expect(qualifier.QualifiedSeconds() == 1.0, "stalled timer sample was not capped to one second");
        Expect(!qualifier.Observe(L"owner-a|track-b", false, 6000), "paused playback qualified history");
        Expect(!qualifier.Observe(L"owner-a|track-b", true, 12000), "resume counted paused wall-clock time");
        Expect(!qualifier.Observe(L"owner-b|track-b", true, 13000), "another owner advanced playback qualification");
    }

    void TestHistoryImportEventIds()
    {
        auto first = account::CreateHistoryImportEventId(L"owner-a", L"remote|source-a");
        auto repeated = account::CreateHistoryImportEventId(L"owner-a", L"remote|source-a");
        auto anotherOwner = account::CreateHistoryImportEventId(L"owner-b", L"remote|source-a");
        Expect(first.size() == 36, "history import event ID is not GUID-shaped");
        Expect(first == repeated, "history import event ID is not stable across retries");
        Expect(first != anotherOwner, "history import event ID is not owner-scoped");
        Expect(first.find(L"source-a") == std::wstring::npos, "history import event ID exposed its source key");
        Expect(first[14] == L'5', "history import event ID did not set the deterministic UUID version");
        Expect(first[19] == L'8' || first[19] == L'9' || first[19] == L'a' || first[19] == L'b',
            "history import event ID did not set the UUID variant");
    }

    void TestRestoreCannotResurrectRemovedSession()
    {
        AccountSessionFixture fixture;
        Expect(fixture.Credentials.WriteAccountSession(L"restore-a"),
            "restore fixture could not store its bearer");

        auto profileGate = std::make_shared<AsyncGate>();
        fixture.Gateway.SetProfile(L"restore-a", Profile(L"owner-a"), profileGate);
        std::vector<std::wstring> ownerChanges;
        fixture.Service.SetOwnerChangedCallback([&ownerChanges](winrt::hstring const& owner)
        {
            ownerChanges.emplace_back(owner.c_str());
        });

        auto restore = fixture.Service.RestoreAsync();
        if (!WaitUntilEntered(profileGate))
        {
            profileGate->Release();
            restore.get();
            Expect(false, "restore did not reach profile validation");
        }

        Expect(fixture.Service.RemoveLocalSession(),
            "local removal failed while restore was suspended");
        profileGate->Release();
        restore.get();

        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::SignedOut,
            "stale restore resurrected a removed session");
        Expect(snapshot.Profile.Id.empty(), "stale restore restored an owner");
        Expect(fixture.Credentials.ReadAccountSession().empty(),
            "stale restore recreated a removed credential");
        Expect(ownerChanges.empty(), "stale restore emitted an owner callback");
    }

    void TestConcurrentSignInsKeepLatestSession()
    {
        AccountSessionFixture fixture;
        auto profileGate = std::make_shared<AsyncGate>();
        fixture.Gateway.QueueAcquire(L"bearer-a");
        fixture.Gateway.SetProfile(L"bearer-a", Profile(L"owner-a"), profileGate);

        std::vector<std::wstring> ownerChanges;
        fixture.Service.SetOwnerChangedCallback([&ownerChanges](winrt::hstring const& owner)
        {
            ownerChanges.emplace_back(owner.c_str());
        });

        auto signInA = fixture.Service.SignInAsync();
        if (!WaitUntilEntered(profileGate))
        {
            profileGate->Release();
            (void)signInA.get();
            Expect(false, "first sign-in did not reach profile validation");
        }

        fixture.Gateway.QueueAcquire(L"bearer-b");
        fixture.Gateway.SetProfile(L"bearer-b", Profile(L"owner-b"));
        Expect(fixture.Service.SignInAsync().get(),
            "newer sign-in did not complete successfully");

        profileGate->Release();
        Expect(!signInA.get(), "superseded sign-in reported success");

        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::Validated,
            "newer sign-in did not remain validated");
        Expect(snapshot.Profile.Id == L"owner-b",
            "older sign-in replaced the newer owner");
        Expect(fixture.Credentials.ReadAccountSession() == L"bearer-b",
            "older sign-in deleted or replaced the newer bearer");
        Expect(ownerChanges.size() == 1 && ownerChanges.front() == L"owner-b",
            "superseded sign-in emitted an out-of-order owner callback");
        Expect(snapshot.LastSafeError.empty(),
            "superseded sign-in overwrote the newer session error state");
    }

    void TestSupersededSignInCannotPublishAfterFinalIntentCheck()
    {
        AccountSessionFixture fixture;
        auto commitGate = std::make_shared<AsyncGate>();
        fixture.Service.SetBeforeValidatedStateCommitForTesting([commitGate]
        {
            commitGate->Reach();
        });
        fixture.Gateway.QueueAcquire(L"bearer-a");
        fixture.Gateway.SetProfile(L"bearer-a", Profile(L"owner-a"));

        std::vector<std::wstring> ownerChanges;
        fixture.Service.SetOwnerChangedCallback([&ownerChanges](winrt::hstring const& owner)
        {
            ownerChanges.emplace_back(owner.c_str());
        });

        auto signInA = fixture.Service.SignInAsync();
        if (!WaitUntilEntered(commitGate))
        {
            commitGate->Release();
            (void)signInA.get();
            Expect(false, "first sign-in did not reach its final state-commit checkpoint");
        }
        auto firstIntent = fixture.Gateway.AcquireIntent();

        fixture.Gateway.QueueAcquire(L"bearer-b", nullptr, E_ABORT);
        auto signInB = std::async(std::launch::async, [&fixture]
        {
            return fixture.Service.SignInAsync().get();
        });
        auto replacementIntentStarted =
            fixture.Gateway.WaitForCancellationAfter(firstIntent);
        commitGate->Release();

        auto firstResult = signInA.get();
        auto secondResult = signInB.get();
        Expect(replacementIntentStarted,
            "replacement sign-in did not supersede the first authorization intent");
        Expect(!firstResult,
            "superseded sign-in published after its final authorization-intent check");
        Expect(!secondResult,
            "scripted replacement sign-in failure reported success");

        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::SignedOut,
            "failed replacement sign-in did not leave the service signed out");
        Expect(snapshot.Profile.Id.empty(),
            "superseded sign-in left its owner published");
        Expect(fixture.Credentials.ReadAccountSession().empty(),
            "superseded sign-in left a credential that restore could resurrect");
        Expect(ownerChanges.empty(),
            "superseded sign-in emitted an owner transition");
    }

    void TestStaleLogoutCannotClearReplacementSession()
    {
        AccountSessionFixture fixture;
        EstablishSession(fixture, L"bearer-a", L"owner-a");

        auto logoutGate = std::make_shared<AsyncGate>();
        fixture.Gateway.SetLogout(L"bearer-a", logoutGate);
        auto logoutA = fixture.Service.LogoutAsync();
        if (!WaitUntilEntered(logoutGate))
        {
            logoutGate->Release();
            (void)logoutA.get();
            Expect(false, "logout did not reach its remote checkpoint");
        }

        fixture.Gateway.QueueAcquire(L"bearer-b");
        fixture.Gateway.SetProfile(L"bearer-b", Profile(L"owner-b"));
        Expect(fixture.Service.SignInAsync().get(),
            "replacement sign-in did not complete during logout");

        logoutGate->Release();
        Expect(!logoutA.get(), "superseded logout reported success");

        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Profile.Id == L"owner-b",
            "stale logout cleared the replacement owner");
        Expect(fixture.Credentials.ReadAccountSession() == L"bearer-b",
            "stale logout deleted the replacement bearer");
    }

    void TestUnauthorizedContextCannotClearAnotherSession()
    {
        AccountSessionFixture fixture;
        EstablishSession(fixture, L"bearer-a", L"owner-a");
        auto oldContext = fixture.Service.CaptureOperation();

        fixture.Gateway.QueueAcquire(L"bearer-b");
        fixture.Gateway.SetProfile(L"bearer-b", Profile(L"owner-b"));
        Expect(fixture.Service.SignInAsync().get(),
            "replacement sign-in failed before unauthorized test");

        fixture.Service.HandleUnauthorized(oldContext);
        auto replacement = fixture.Service.Snapshot();
        Expect(replacement.Profile.Id == L"owner-b",
            "stale unauthorized response cleared another owner");
        Expect(fixture.Credentials.ReadAccountSession() == L"bearer-b",
            "stale unauthorized response deleted another bearer");

        auto currentContext = fixture.Service.CaptureOperation();
        fixture.Service.HandleUnauthorized(currentContext);
        auto cleared = fixture.Service.Snapshot();
        Expect(cleared.Status == account::AccountSessionStatus::SignedOut,
            "current unauthorized response did not clear the session");
        Expect(fixture.Credentials.ReadAccountSession().empty(),
            "current unauthorized response left its bearer stored");
    }

    void TestCredentialDeleteFailurePreservesSession()
    {
        AccountSessionFixture fixture;
        EstablishSession(fixture, L"bearer-a", L"owner-a");
        auto context = fixture.Service.CaptureOperation();

        fixture.Api->failDelete = true;
        Expect(!fixture.Service.RemoveLocalSession(),
            "failed credential deletion reported local removal success");
        auto snapshot = fixture.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::Validated,
            "failed credential deletion cleared the in-memory session");
        Expect(snapshot.Profile.Id == L"owner-a",
            "failed credential deletion cleared the owner");
        Expect(fixture.Service.IsCurrent(context),
            "failed credential deletion invalidated the usable session");
        Expect(fixture.Credentials.ReadAccountSession() == L"bearer-a",
            "failed credential deletion removed the bearer");
    }

    void TestOwnerCallbackExceptionsAreIsolated()
    {
        AccountSessionFixture fixture;
        fixture.Service.SetOwnerChangedCallback([](winrt::hstring const&)
        {
            throw std::runtime_error("scripted owner callback failure");
        });
        EstablishSession(fixture, L"bearer-a", L"owner-a");
        Expect(fixture.Service.Snapshot().Profile.Id == L"owner-a",
            "owner callback exception rolled back the committed session");
    }

    void TestRemoteScopeCacheKeys()
    {
        account::RemoteScopeSnapshot apiKeyScope{
            account::RemoteAccessMode::ApiKey,
            7,
            11,
            L"partition-a"
        };
        auto sameApiKeyScope = apiKeyScope;
        sameApiKeyScope.AccountGeneration = 12;
        Expect(account::RemoteScopeCacheKey(apiKeyScope)
                == account::RemoteScopeCacheKey(sameApiKeyScope),
            "API-key cache scope depended on an unrelated account generation");

        auto changedApiKeyScope = apiKeyScope;
        changedApiKeyScope.Generation = 8;
        Expect(account::RemoteScopeCacheKey(apiKeyScope)
                != account::RemoteScopeCacheKey(changedApiKeyScope),
            "API-key cache scope survived remote-scope invalidation");

        auto changedApiKeyPartition = apiKeyScope;
        changedApiKeyPartition.CachePartition = L"partition-b";
        Expect(account::RemoteScopeCacheKey(apiKeyScope)
                != account::RemoteScopeCacheKey(changedApiKeyPartition),
            "API-key cache scope ignored its provider configuration partition");

        account::RemoteScopeSnapshot accountScope{
            account::RemoteAccessMode::Account,
            7,
            11,
            {}
        };
        auto replacementAccountScope = accountScope;
        replacementAccountScope.AccountGeneration = 12;
        Expect(account::RemoteScopeCacheKey(accountScope)
                != account::RemoteScopeCacheKey(replacementAccountScope),
            "Account cache scope survived account replacement");
        Expect(account::RemoteScopeCacheKey(accountScope)
                != account::RemoteScopeCacheKey(apiKeyScope),
            "Account and API-key cache scopes overlapped");
    }

    void TestApiKeyScopePartitionsProviderConfiguration()
    {
        AccountSessionFixture fixture;
        fixture.Settings.SetString(
            L"ProviderBaseUrl",
            L"https://provider.example/");
        Expect(fixture.Credentials.WriteProviderApiKey(L"provider-secret-a"),
            "API-key scope test could not store its provider credential");
        fixture.Settings.SetString(L"RemoteAccessMode", L"ApiKey");

        account::AccountClient client;
        ScriptedAccountSyncTransport transport;
        account::RemoteMusicService service{
            fixture.Settings,
            fixture.Credentials,
            fixture.Service,
            client,
            transport
        };
        auto originalScope = service.CaptureScope();
        auto originalCacheKey = account::RemoteScopeCacheKey(originalScope);
        Expect(originalScope.Mode == account::RemoteAccessMode::ApiKey,
            "configured provider did not produce an API-key scope");
        Expect(service.IsCurrent(originalScope),
            "fresh API-key scope was not current");
        Expect(originalScope.CachePartition.size() == 64,
            "API-key scope did not contain a SHA-256 partition");
        Expect(originalScope.CachePartition.find(L"provider-secret-a") == std::wstring::npos,
            "API-key cache partition exposed the provider credential");
        Expect(originalCacheKey.find(L"provider-secret-a") == std::wstring::npos,
            "API-key cache key exposed the provider credential");

        account::RemoteMusicService restartedService{
            fixture.Settings,
            fixture.Credentials,
            fixture.Service,
            client,
            transport
        };
        auto restartedScope = restartedService.CaptureScope();
        Expect(restartedScope.Generation == originalScope.Generation,
            "restart-equivalent remote services did not start at the same generation");
        Expect(restartedScope.CachePartition == originalScope.CachePartition,
            "identical provider configuration produced a different restart partition");
        Expect(account::RemoteScopeCacheKey(restartedScope) == originalCacheKey,
            "identical restart-equivalent scopes produced different cache keys");

        fixture.Settings.SetString(
            L"ProviderBaseUrl",
            L"https://replacement-provider.example");
        Expect(!service.IsCurrent(originalScope),
            "provider-origin replacement left the previous API-key scope current");
        auto replacedOriginScope = service.CaptureScope();
        Expect(replacedOriginScope.Generation == originalScope.Generation,
            "provider-origin test unexpectedly relied on generation invalidation");
        Expect(replacedOriginScope.CachePartition != originalScope.CachePartition,
            "provider-origin replacement reused the previous cache partition");
        Expect(service.IsCurrent(replacedOriginScope),
            "replacement provider origin did not produce a current scope");

        Expect(fixture.Credentials.WriteProviderApiKey(L"provider-secret-b"),
            "API-key scope test could not replace its provider credential");
        Expect(!service.IsCurrent(replacedOriginScope),
            "provider-credential replacement left the previous API-key scope current");
        auto replacedCredentialScope = service.CaptureScope();
        Expect(replacedCredentialScope.Generation == replacedOriginScope.Generation,
            "provider-credential test unexpectedly relied on generation invalidation");
        Expect(replacedCredentialScope.CachePartition != replacedOriginScope.CachePartition,
            "provider-credential replacement reused the previous cache partition");
        Expect(replacedCredentialScope.CachePartition.find(L"provider-secret-b") == std::wstring::npos,
            "replacement API-key cache partition exposed the provider credential");
        Expect(account::RemoteScopeCacheKey(replacedCredentialScope).find(L"provider-secret-b")
                == std::wstring::npos,
            "replacement API-key cache key exposed the provider credential");

        Expect(fixture.Settings.Remove(L"ProviderBaseUrl"),
            "API-key scope test could not remove its provider origin");
        auto incompleteScope = service.CaptureScope();
        Expect(incompleteScope.CachePartition.empty(),
            "incomplete provider configuration produced a cache partition");
        Expect(!service.IsCurrent(incompleteScope),
            "incomplete API-key configuration produced a current remote scope");
    }

    void TestAccountPlaylistScopeRejectsReplacementBeforeDispatch()
    {
        RemoteAccountFixture fixture;
        auto staleScope = fixture.Remote.CaptureScope();
        fixture.ReplaceSession();

        auto operation = fixture.Remote.UpdateAccountPlaylistAsync(
            staleScope,
            L"playlist-a",
            L"{\"name\":\"Updated\"}");
        ExpectCanceled(operation,
            "a playlist captured under a replaced account reached the account client");
    }

    void TestAccountSyncContextBindsBearerAndPayloads()
    {
        RemoteAccountFixture fixture;
        auto context = fixture.Remote.CaptureAccountSyncContext();

        fixture.Transport.SetLibraryScript({ L"library-response" });
        Expect(fixture.Remote.GetAccountLibraryAsync(context).get() == L"library-response",
            "library synchronization returned the wrong payload");

        fixture.Transport.SetLikeScript({ L"like-response" });
        Expect(fixture.Remote.SetAccountLikedAsync(
            context,
            L"{\"id\":\"track-a\"}",
            true,
            L"remote-a").get() == L"like-response",
            "like synchronization returned the wrong payload");

        fixture.Transport.SetHistoryScript({ L"history-response" });
        Expect(fixture.Remote.PostAccountHistoryBatchAsync(
            context,
            L"{\"events\":[\"event-a\"]}").get() == L"history-response",
            "history synchronization returned the wrong payload");

        auto calls = fixture.Transport.Calls();
        Expect(calls.size() == 3, "account synchronization did not dispatch every request");
        Expect(calls[0].Kind == ScriptedAccountSyncTransport::CallKind::Library,
            "library synchronization used the wrong transport method");
        Expect(calls[1].Kind == ScriptedAccountSyncTransport::CallKind::Like,
            "like synchronization used the wrong transport method");
        Expect(calls[2].Kind == ScriptedAccountSyncTransport::CallKind::History,
            "history synchronization used the wrong transport method");
        for (auto const& call : calls)
        {
            Expect(call.Bearer == L"bearer-a",
                "account synchronization dispatched a bearer from another session");
        }
        Expect(calls[1].Body == L"{\"id\":\"track-a\"}",
            "like synchronization changed the submitted track payload");
        Expect(calls[1].Liked && calls[1].RemoteId == L"remote-a",
            "like synchronization changed its submitted mutation");
        Expect(calls[2].Body == L"{\"events\":[\"event-a\"]}",
            "history synchronization changed the submitted batch");
    }

    void TestAccountSyncContextRejectsReplacementBeforeDispatch()
    {
        RemoteAccountFixture fixture;
        auto staleContext = fixture.Remote.CaptureAccountSyncContext();
        fixture.ReplaceSession();

        auto operation = fixture.Remote.PostAccountHistoryBatchAsync(
            staleContext,
            L"{\"events\":[]}");
        ExpectCanceled(operation,
            "a replaced account context reached the synchronization transport");
        Expect(fixture.Transport.Calls().empty(),
            "a replaced account context dispatched a synchronization request");
    }

    void TestAccountSyncContextRejectsInFlightReplacement()
    {
        RemoteAccountFixture fixture;
        auto context = fixture.Remote.CaptureAccountSyncContext();
        auto transportGate = std::make_shared<AsyncGate>();
        fixture.Transport.SetHistoryScript({ L"history-response", transportGate });

        auto operation = fixture.Remote.PostAccountHistoryBatchAsync(
            context,
            L"{\"events\":[\"event-a\"]}");
        ExpectTransportReached(
            transportGate,
            operation,
            "history synchronization did not reach its transport checkpoint");

        fixture.ReplaceSession();
        transportGate->Release();
        ExpectCanceled(operation,
            "an in-flight response from a replaced account was accepted");

        auto calls = fixture.Transport.Calls();
        Expect(calls.size() == 1 && calls.front().Bearer == L"bearer-a",
            "an in-flight account request changed bearer after dispatch");
        auto snapshot = fixture.Session.Service.Snapshot();
        Expect(snapshot.Profile.Id == L"owner-b",
            "an in-flight response replaced the newer account owner");
        Expect(fixture.Session.Credentials.ReadAccountSession() == L"bearer-b",
            "an in-flight response replaced the newer account bearer");
    }

    void TestAccountSyncContextRejectsRemoteModeChanges()
    {
        {
            RemoteAccountFixture fixture;
            auto context = fixture.Remote.CaptureAccountSyncContext();
            auto transportGate = std::make_shared<AsyncGate>();
            fixture.Transport.SetHistoryScript({ L"history-response", transportGate });
            auto operation = fixture.Remote.PostAccountHistoryBatchAsync(context, L"{}");
            ExpectTransportReached(
                transportGate,
                operation,
                "LocalOnly invalidation test did not reach its transport checkpoint");

            Expect(fixture.Remote.SetMode(account::RemoteAccessMode::LocalOnly),
                "remote service could not enter LocalOnly mode");
            transportGate->Release();
            ExpectCanceled(operation,
                "Account response was accepted after switching to LocalOnly mode");
        }

        {
            RemoteAccountFixture fixture;
            auto context = fixture.Remote.CaptureAccountSyncContext();
            auto transportGate = std::make_shared<AsyncGate>();
            fixture.Transport.SetHistoryScript({ L"history-response", transportGate });
            auto operation = fixture.Remote.PostAccountHistoryBatchAsync(context, L"{}");
            ExpectTransportReached(
                transportGate,
                operation,
                "API-key invalidation test did not reach its transport checkpoint");

            fixture.Session.Settings.SetString(
                L"ProviderBaseUrl",
                L"https://provider.example");
            Expect(fixture.Session.Credentials.WriteProviderApiKey(L"provider-key"),
                "API-key invalidation test could not store its provider credential");
            Expect(fixture.Remote.SetMode(account::RemoteAccessMode::ApiKey),
                "remote service could not enter API-key mode");
            transportGate->Release();
            ExpectCanceled(operation,
                "Account response was accepted after switching to API-key mode");
        }

        {
            RemoteAccountFixture fixture;
            auto context = fixture.Remote.CaptureAccountSyncContext();
            auto transportGate = std::make_shared<AsyncGate>();
            fixture.Transport.SetHistoryScript({ L"history-response", transportGate });
            auto operation = fixture.Remote.PostAccountHistoryBatchAsync(context, L"{}");
            ExpectTransportReached(
                transportGate,
                operation,
                "remote-mode ABA test did not reach its transport checkpoint");

            Expect(fixture.Remote.SetMode(account::RemoteAccessMode::LocalOnly),
                "remote-mode ABA test could not leave Account mode");
            Expect(fixture.Remote.SetMode(account::RemoteAccessMode::Account),
                "remote-mode ABA test could not return to Account mode");
            transportGate->Release();
            ExpectCanceled(operation,
                "Account response survived a remote-mode ABA change");
        }
    }

    void TestStaleSyncUnauthorizedCannotClearReplacementSession()
    {
        RemoteAccountFixture fixture;
        auto context = fixture.Remote.CaptureAccountSyncContext();
        auto transportGate = std::make_shared<AsyncGate>();
        fixture.Transport.SetHistoryScript({
            {},
            transportGate,
            HRESULT_FROM_WIN32(ERROR_LOGON_FAILURE)
        });

        auto operation = fixture.Remote.PostAccountHistoryBatchAsync(context, L"{}");
        ExpectTransportReached(
            transportGate,
            operation,
            "unauthorized synchronization test did not reach its transport checkpoint");
        fixture.ReplaceSession();
        transportGate->Release();
        ExpectCanceled(operation,
            "stale unauthorized synchronization response escaped as a current error");

        auto snapshot = fixture.Session.Service.Snapshot();
        Expect(snapshot.Status == account::AccountSessionStatus::Validated,
            "stale unauthorized synchronization response signed out the replacement session");
        Expect(snapshot.Profile.Id == L"owner-b",
            "stale unauthorized synchronization response cleared the replacement owner");
        Expect(fixture.Session.Credentials.ReadAccountSession() == L"bearer-b",
            "stale unauthorized synchronization response deleted the replacement bearer");
    }

    void TestStreamCacheRejectsStaleAttemptPublication(
        std::filesystem::path const& localAppData)
    {
        auto cacheDirectory = localAppData / L"Last Music Player" / L"stream-cache";
        std::error_code ec;
        std::filesystem::remove_all(cacheDirectory, ec);

        account::UserDataOperationGate operationGate;
        auto transport = std::make_shared<ScriptedStreamCacheTransport>();
        auto staleGate = std::make_shared<AsyncGate>();
        auto currentGate = std::make_shared<AsyncGate>();
        transport->Queue({ staleGate, "stale", L".mp3" });
        transport->Queue({ currentGate, "current", L".m4a" });

        account::StreamCache cache{ operationGate, transport };
        std::wstring const sourceKey = L"ApiKey:7\nhttps://media.example/track";
        cache.Prefetch(sourceKey, L"https://stream.example/stale");
        Expect(WaitUntilEntered(staleGate),
            "the stale stream-cache attempt did not reach its transfer checkpoint");

        cache.InvalidateInFlight();
        cache.Prefetch(sourceKey, L"https://stream.example/current");
        Expect(WaitUntilEntered(currentGate),
            "the replacement stream-cache attempt did not reach its transfer checkpoint");
        currentGate->Release();

        std::wstring readyPath;
        auto readyDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < readyDeadline)
        {
            readyPath = cache.ReadyPath(sourceKey);
            if (!readyPath.empty())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        Expect(!readyPath.empty(),
            "the replacement stream-cache attempt was not published");
        Expect(ReadFile(readyPath) == "current",
            "the replacement stream-cache attempt published the wrong bytes");

        Expect(operationGate.CloseAdmissions(),
            "stream-cache cleanup could not close operation admissions");
        Expect(!cache.Clear(),
            "stream-cache cleanup deleted files while a stale transfer was active");
        staleGate->Release();
        Expect(operationGate.WaitForIdle(std::chrono::seconds(2)),
            "the canceled stream-cache attempt did not release its operation lease");

        auto survivingPath = cache.ReadyPath(sourceKey);
        Expect(survivingPath == readyPath && ReadFile(survivingPath) == "current",
            "a stale stream-cache attempt replaced or deleted the current result");
        Expect(cache.Clear(),
            "stream-cache cleanup failed after every transfer became idle");
        operationGate.Reopen();
        Expect(!std::filesystem::exists(cacheDirectory),
            "stream-cache cleanup left its directory behind");
    }

    void TestStreamCacheRetiredTransfersCountTowardCap(
        std::filesystem::path const& localAppData)
    {
        auto cacheDirectory = localAppData / L"Last Music Player" / L"stream-cache";
        std::error_code ec;
        std::filesystem::remove_all(cacheDirectory, ec);

        account::UserDataOperationGate operationGate;
        auto transport = std::make_shared<ScriptedStreamCacheTransport>();
        auto firstGate = std::make_shared<AsyncGate>();
        auto secondGate = std::make_shared<AsyncGate>();
        auto thirdGate = std::make_shared<AsyncGate>();
        transport->Queue({ firstGate, "first", L".mp3" });
        transport->Queue({ secondGate, "second", L".mp3" });
        transport->Queue({ thirdGate, "third", L".mp3" });

        account::StreamCache cache{ operationGate, transport };
        cache.Prefetch(
            L"ApiKey:partition:7\nhttps://media.example/first",
            L"https://stream.example/first");
        cache.Prefetch(
            L"ApiKey:partition:7\nhttps://media.example/second",
            L"https://stream.example/second");
        auto firstStarted = WaitUntilEntered(firstGate);
        auto secondStarted = WaitUntilEntered(secondGate);

        cache.InvalidateInFlight();
        cache.Prefetch(
            L"ApiKey:partition:8\nhttps://media.example/third",
            L"https://stream.example/third");
        auto thirdStartedWhileFull = thirdGate->Entered.wait_for(
            std::chrono::milliseconds(250)) == std::future_status::ready;

        firstGate->Release();
        auto thirdDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!thirdStartedWhileFull
            && thirdGate->Entered.wait_for(std::chrono::milliseconds(0))
                != std::future_status::ready
            && std::chrono::steady_clock::now() < thirdDeadline)
        {
            cache.Prefetch(
                L"ApiKey:partition:8\nhttps://media.example/third",
                L"https://stream.example/third");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        auto thirdStartedAfterSlotReleased =
            thirdGate->Entered.wait_for(std::chrono::milliseconds(0))
                == std::future_status::ready;

        secondGate->Release();
        thirdGate->Release();
        auto cleanupOwned = operationGate.CloseAdmissions();
        auto becameIdle = operationGate.WaitForIdle(std::chrono::seconds(2));
        auto cleared = cache.Clear();
        operationGate.Reopen();

        Expect(firstStarted && secondStarted,
            "stream-cache cap test did not start its first two transfers");
        Expect(!thirdStartedWhileFull,
            "invalidated stream-cache transfers stopped counting toward the active cap");
        Expect(thirdStartedAfterSlotReleased,
            "stream-cache did not admit a transfer after a retired child returned");
        Expect(cleanupOwned,
            "stream-cache cap test could not close cleanup admissions");
        Expect(becameIdle,
            "stream-cache cap test transfers did not release their operation leases");
        Expect(cleared,
            "stream-cache cap test could not clear completed transfers");
        Expect(!std::filesystem::exists(cacheDirectory),
            "stream-cache cap test left its directory behind");
    }

    void TestUserDataOperationGate()
    {
        account::UserDataOperationGate gate;
        auto admission = gate.TryEnter();
        Expect(admission.has_value(), "operation gate rejected work while open");
        Expect(gate.IsAccepting(), "operation gate did not report its open state");
        auto initialGeneration = gate.Generation();
        Expect(gate.IsCurrent(initialGeneration),
            "operation gate rejected its current generation");

        Expect(gate.CloseAdmissions(), "operation gate did not grant cleanup ownership");
        auto closedGeneration = gate.Generation();
        Expect(closedGeneration != initialGeneration,
            "operation gate closure did not invalidate stale continuations");
        Expect(!gate.IsCurrent(initialGeneration),
            "operation gate kept a pre-cleanup continuation current");
        Expect(!gate.CloseAdmissions(), "operation gate granted overlapping cleanup ownership");
        Expect(gate.Generation() == closedGeneration,
            "overlapping cleanup changed the operation generation");
        Expect(!gate.IsAccepting(), "operation gate remained open after closure");
        Expect(!gate.TryEnter().has_value(), "operation gate admitted work after closure");
        Expect(!gate.WaitForIdle(std::chrono::milliseconds(0)),
            "operation gate reported idle while a lease was active");

        std::promise<void> releaseSignal;
        auto releaseFuture = releaseSignal.get_future();
        auto lease = std::move(*admission);
        std::thread releaseThread{
            [lease = std::move(lease), releaseFuture = std::move(releaseFuture)]() mutable
            {
                releaseFuture.wait();
                lease = account::UserDataOperationGate::Lease{};
            }
        };

        releaseSignal.set_value();
        auto becameIdle = gate.WaitForIdle(std::chrono::seconds(2));
        releaseThread.join();
        Expect(becameIdle,
            "operation gate did not observe a cross-thread lease release");

        gate.Reopen();
        Expect(gate.IsAccepting(), "operation gate did not reopen");
        Expect(gate.IsCurrent(closedGeneration),
            "operation gate invalidated work started after cleanup closure");
        Expect(!gate.IsCurrent(initialGeneration),
            "operation gate revived a pre-cleanup continuation after reopening");
        Expect(gate.TryEnter().has_value(), "operation gate rejected work after reopening");
    }

}

int wmain()
{
    auto localAppData = std::filesystem::temp_directory_path()
        / (L"LastMusicNativeAccountTests-" + std::to_wstring(::GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(localAppData, ec);
    std::filesystem::create_directories(localAppData, ec);
    if (ec)
    {
        std::wcerr << L"Unable to create test directory: " << ec.message().c_str() << L"\n";
        return 1;
    }

    wchar_t* previousLocalAppData{};
    size_t previousLocalAppDataLength{};
    _wdupenv_s(&previousLocalAppData, &previousLocalAppDataLength, L"LOCALAPPDATA");
    std::wstring oldLocalAppData = previousLocalAppData ? previousLocalAppData : L"";
    std::free(previousLocalAppData);
    _wputenv_s(L"LOCALAPPDATA", localAppData.c_str());

    try
    {
        winrt::init_apartment();
        TestCredentialReadWriteDelete();
        TestSuccessfulLegacyMigration(localAppData);
        TestPkce();
        TestLoopbackRequestParsing();
        TestConfiguredAccountBuild();
        TestAccountGatewayProtocol();
        TestLoopbackListenerLifecycle();
        TestAccountRedirectProtection();
        TestAccountOriginAndSafeErrors();
        TestInlineProfileImageValidation();
        TestProfileIdentitySelection();
        TestSyncableRemoteSources();
        TestCatalogDiscoveryParsing();
        TestCatalogChartAndDetailParsing();
        TestCatalogTrustBoundaryValidation();
        TestCatalogChartRequestConstruction();
        TestCatalogPresentationRules();
        TestCatalogParsingSurvivesBadInput();
        TestFailedSecureWritePreservesLegacySource(localAppData);
        TestFailedVerificationPreservesLegacySource();
        TestOwnerScopedAccountSchema();
        TestPlaybackHistoryQualification();
        TestHistoryImportEventIds();
        TestRestoreCannotResurrectRemovedSession();
        TestConcurrentSignInsKeepLatestSession();
        TestSupersededSignInCannotPublishAfterFinalIntentCheck();
        TestStaleLogoutCannotClearReplacementSession();
        TestUnauthorizedContextCannotClearAnotherSession();
        TestCredentialDeleteFailurePreservesSession();
        TestOwnerCallbackExceptionsAreIsolated();
        TestRemoteScopeCacheKeys();
        TestApiKeyScopePartitionsProviderConfiguration();
        TestAccountPlaylistScopeRejectsReplacementBeforeDispatch();
        TestAccountSyncContextBindsBearerAndPayloads();
        TestAccountSyncContextRejectsReplacementBeforeDispatch();
        TestAccountSyncContextRejectsInFlightReplacement();
        TestAccountSyncContextRejectsRemoteModeChanges();
        TestStaleSyncUnauthorizedCannotClearReplacementSession();
        TestStreamCacheRejectsStaleAttemptPublication(localAppData);
        TestStreamCacheRetiredTransfersCountTowardCap(localAppData);
        TestUserDataOperationGate();
        std::wcout << L"Native account sync tests passed.\n";
    }
    catch (std::exception const& ex)
    {
        std::cerr << "Native account credential tests failed: " << ex.what() << "\n";
        _wputenv_s(L"LOCALAPPDATA", oldLocalAppData.c_str());
        std::filesystem::remove_all(localAppData, ec);
        return 1;
    }

    _wputenv_s(L"LOCALAPPDATA", oldLocalAppData.c_str());
    std::filesystem::remove_all(localAppData, ec);
    return 0;
}
