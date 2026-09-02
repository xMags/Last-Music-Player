#include "pch.h"
#include "Backend/CastDiscovery.h"

#include <windows.h>
#include <windns.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <utility>

#pragma comment(lib, "dnsapi.lib")

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr wchar_t kCastServiceName[] = L"_googlecast._tcp.local";
        constexpr DWORD kCancellationCallbackWaitMs = 5000;

        class UniqueHandle final
        {
        public:
            UniqueHandle() = default;
            explicit UniqueHandle(HANDLE value) noexcept : m_value(value) {}
            ~UniqueHandle()
            {
                if (m_value != nullptr)
                {
                    ::CloseHandle(m_value);
                }
            }

            UniqueHandle(UniqueHandle const&) = delete;
            UniqueHandle& operator=(UniqueHandle const&) = delete;
            UniqueHandle(UniqueHandle&& other) noexcept
                : m_value(std::exchange(other.m_value, nullptr))
            {
            }
            UniqueHandle& operator=(UniqueHandle&& other) noexcept
            {
                if (this != &other)
                {
                    if (m_value != nullptr)
                    {
                        ::CloseHandle(m_value);
                    }
                    m_value = std::exchange(other.m_value, nullptr);
                }
                return *this;
            }

            [[nodiscard]] HANDLE Get() const noexcept { return m_value; }
            [[nodiscard]] explicit operator bool() const noexcept { return m_value != nullptr; }

        private:
            HANDLE m_value{};
        };

        struct BrowseState final
        {
            mutable std::mutex Mutex;
            std::set<std::wstring> Instances;
            UniqueHandle CompletionEvent;
            DNS_SERVICE_CANCEL Cancel{};
            std::atomic<bool> Completed{};
            std::atomic<bool> CancellationRequested{};
        };

        struct ResolveState final
        {
            mutable std::mutex Mutex;
            std::optional<CastDevice> Device;
            std::wstring QueryName;
            UniqueHandle CompletionEvent;
            DNS_SERVICE_CANCEL Cancel{};
            std::atomic<bool> Completed{};
            std::atomic<bool> CancellationRequested{};
        };

        template <typename State>
        struct CallbackLease final
        {
            std::shared_ptr<State> Value;
        };

        void CompleteBrowse(
            CallbackLease<BrowseState>* lease,
            std::shared_ptr<BrowseState> const& state) noexcept
        {
            if (!state->Completed.exchange(true))
            {
                ::SetEvent(state->CompletionEvent.Get());
                delete lease;
            }
        }

        VOID WINAPI BrowseCallback(DWORD status, PVOID context, PDNS_RECORD records)
        {
            auto* lease = static_cast<CallbackLease<BrowseState>*>(context);
            auto state = lease ? lease->Value : nullptr;
            if (state && status == ERROR_SUCCESS && records != nullptr)
            {
                std::lock_guard guard{ state->Mutex };
                for (auto* record = records; record != nullptr; record = record->pNext)
                {
                    if (record->wType == DNS_TYPE_PTR && record->Data.PTR.pNameHost != nullptr)
                    {
                        state->Instances.insert(record->Data.PTR.pNameHost);
                    }
                }
            }
            if (records != nullptr)
            {
                ::DnsRecordListFree(records, DnsFreeRecordList);
            }
            if (state && status != ERROR_SUCCESS)
            {
                CompleteBrowse(lease, state);
            }
        }

        std::optional<CastDevice> ReadDevice(PDNS_SERVICE_INSTANCE instance)
        {
            if (instance == nullptr)
            {
                return std::nullopt;
            }

            CastDevice device;
            if (instance->ip4Address != nullptr)
            {
                auto const ip = *instance->ip4Address;
                wchar_t address[32]{};
                swprintf_s(
                    address,
                    L"%u.%u.%u.%u",
                    ip & 0xFF,
                    (ip >> 8) & 0xFF,
                    (ip >> 16) & 0xFF,
                    (ip >> 24) & 0xFF);
                device.host = address;
            }
            else if (instance->pszHostName != nullptr)
            {
                device.host = instance->pszHostName;
            }
            device.port = instance->wPort != 0 ? instance->wPort : 8009;

            std::wstring friendlyName;
            std::wstring deviceId;
            for (DWORD index = 0; index < instance->dwPropertyCount; ++index)
            {
                if (instance->keys == nullptr
                    || instance->values == nullptr
                    || instance->keys[index] == nullptr)
                {
                    continue;
                }
                std::wstring const key = instance->keys[index];
                std::wstring const value = instance->values[index] != nullptr
                    ? instance->values[index]
                    : L"";
                if (_wcsicmp(key.c_str(), L"fn") == 0)
                {
                    friendlyName = value;
                }
                else if (_wcsicmp(key.c_str(), L"id") == 0)
                {
                    deviceId = value;
                }
            }

            std::wstring label = instance->pszInstanceName != nullptr
                ? instance->pszInstanceName
                : L"";
            auto const dot = label.find(L'.');
            if (dot != std::wstring::npos)
            {
                label.resize(dot);
            }

            device.name = friendlyName.empty() ? label : friendlyName;
            device.id = deviceId.empty()
                ? (instance->pszInstanceName != nullptr
                    ? instance->pszInstanceName
                    : device.host)
                : deviceId;
            if (device.host.empty())
            {
                return std::nullopt;
            }
            return device;
        }

        VOID WINAPI ResolveCallback(
            DWORD status,
            PVOID context,
            PDNS_SERVICE_INSTANCE instance)
        {
            std::unique_ptr<CallbackLease<ResolveState>> lease{
                static_cast<CallbackLease<ResolveState>*>(context) };
            auto state = lease ? lease->Value : nullptr;
            if (state && status == ERROR_SUCCESS)
            {
                auto device = ReadDevice(instance);
                std::lock_guard guard{ state->Mutex };
                state->Device = std::move(device);
            }
            if (instance != nullptr)
            {
                ::DnsServiceFreeInstance(instance);
            }
            if (state && !state->Completed.exchange(true))
            {
                ::SetEvent(state->CompletionEvent.Get());
            }
        }

        class WindowsBrowseOperation final : public IDnsSdBrowseOperation
        {
        public:
            explicit WindowsBrowseOperation(std::shared_ptr<BrowseState> state)
                : m_state(std::move(state)) {}
            ~WindowsBrowseOperation() override { CancelAndWait(); }

            bool Wait(std::uint32_t timeoutMs) override
            {
                return ::WaitForSingleObject(m_state->CompletionEvent.Get(), timeoutMs)
                    == WAIT_OBJECT_0;
            }

            void CancelAndWait() noexcept override
            {
                if (m_state->Completed.load())
                {
                    return;
                }
                if (!m_state->CancellationRequested.exchange(true))
                {
                    if (::DnsServiceBrowseCancel(&m_state->Cancel) != ERROR_SUCCESS)
                    {
                        return;
                    }
                }
                (void)::WaitForSingleObject(
                    m_state->CompletionEvent.Get(),
                    kCancellationCallbackWaitMs);
            }

            std::vector<std::wstring> Instances() const override
            {
                std::lock_guard guard{ m_state->Mutex };
                return { m_state->Instances.begin(), m_state->Instances.end() };
            }

        private:
            std::shared_ptr<BrowseState> m_state;
        };

        class WindowsResolveOperation final : public IDnsSdResolveOperation
        {
        public:
            explicit WindowsResolveOperation(std::shared_ptr<ResolveState> state)
                : m_state(std::move(state)) {}
            ~WindowsResolveOperation() override { CancelAndWait(); }

            bool Wait(std::uint32_t timeoutMs) override
            {
                return ::WaitForSingleObject(m_state->CompletionEvent.Get(), timeoutMs)
                    == WAIT_OBJECT_0;
            }

            void CancelAndWait() noexcept override
            {
                if (m_state->Completed.load())
                {
                    return;
                }
                if (!m_state->CancellationRequested.exchange(true))
                {
                    if (::DnsServiceResolveCancel(&m_state->Cancel) != ERROR_SUCCESS)
                    {
                        return;
                    }
                }
                (void)::WaitForSingleObject(
                    m_state->CompletionEvent.Get(),
                    kCancellationCallbackWaitMs);
            }

            std::optional<CastDevice> Device() const override
            {
                std::lock_guard guard{ m_state->Mutex };
                return m_state->Device;
            }

        private:
            std::shared_ptr<ResolveState> m_state;
        };

        class WindowsDnsSdAdapter final : public IDnsSdAdapter
        {
        public:
            std::unique_ptr<IDnsSdBrowseOperation> BeginBrowse() override
            {
                auto state = std::make_shared<BrowseState>();
                state->CompletionEvent = UniqueHandle{
                    ::CreateEventW(nullptr, TRUE, FALSE, nullptr) };
                if (!state->CompletionEvent)
                {
                    return nullptr;
                }

                auto* lease = new (std::nothrow) CallbackLease<BrowseState>{ state };
                if (lease == nullptr)
                {
                    return nullptr;
                }

                DNS_SERVICE_BROWSE_REQUEST request{};
                request.Version = DNS_QUERY_REQUEST_VERSION1;
                request.InterfaceIndex = 0;
                request.QueryName = kCastServiceName;
                request.pBrowseCallback = &BrowseCallback;
                request.pQueryContext = lease;
                auto const status = ::DnsServiceBrowse(&request, &state->Cancel);
                if (status != DNS_REQUEST_PENDING)
                {
                    delete lease;
                    return nullptr;
                }
                return std::make_unique<WindowsBrowseOperation>(std::move(state));
            }

            std::unique_ptr<IDnsSdResolveOperation> BeginResolve(
                std::wstring const& instance) override
            {
                auto state = std::make_shared<ResolveState>();
                state->QueryName = instance;
                state->CompletionEvent = UniqueHandle{
                    ::CreateEventW(nullptr, TRUE, FALSE, nullptr) };
                if (!state->CompletionEvent)
                {
                    return nullptr;
                }

                auto* lease = new (std::nothrow) CallbackLease<ResolveState>{ state };
                if (lease == nullptr)
                {
                    return nullptr;
                }

                DNS_SERVICE_RESOLVE_REQUEST request{};
                request.Version = DNS_QUERY_REQUEST_VERSION1;
                request.InterfaceIndex = 0;
                request.QueryName = state->QueryName.data();
                request.pResolveCompletionCallback = &ResolveCallback;
                request.pQueryContext = lease;
                auto const status = ::DnsServiceResolve(&request, &state->Cancel);
                if (status != DNS_REQUEST_PENDING)
                {
                    delete lease;
                    return nullptr;
                }
                return std::make_unique<WindowsResolveOperation>(std::move(state));
            }
        };
    }

    std::shared_ptr<IDnsSdAdapter> CreateWindowsDnsSdAdapter()
    {
        return std::make_shared<WindowsDnsSdAdapter>();
    }
}
