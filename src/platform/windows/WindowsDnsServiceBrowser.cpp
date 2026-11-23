#include "platform/windows/WindowsDnsServiceBrowser.h"

#include <windows.h>
#include <windns.h>

#include <QSet>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <utility>

namespace lmp::platform::windows {

namespace {

// How long a cancel waits for the system's final callback.
constexpr DWORD CancellationCallbackWaitMs = 5000;

class UniqueEvent final {
public:
    UniqueEvent() : m_value(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~UniqueEvent()
    {
        if (m_value != nullptr) {
            CloseHandle(m_value);
        }
    }
    UniqueEvent(const UniqueEvent&) = delete;
    UniqueEvent& operator=(const UniqueEvent&) = delete;

    [[nodiscard]] HANDLE get() const noexcept { return m_value; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_value != nullptr; }

private:
    HANDLE m_value = nullptr;
};

struct BrowseState final {
    mutable std::mutex mutex;
    QSet<QString> instances;
    UniqueEvent completion;
    DNS_SERVICE_CANCEL cancel{};
    std::atomic<bool> completed{false};
    std::atomic<bool> cancellationRequested{false};
    std::wstring queryName;
};

struct ResolveState final {
    mutable std::mutex mutex;
    std::optional<DnsServiceRecord> record;
    UniqueEvent completion;
    DNS_SERVICE_CANCEL cancel{};
    std::atomic<bool> completed{false};
    std::atomic<bool> cancellationRequested{false};
    std::wstring queryName;
};

// What the system callback holds: a share of the operation's state, so it
// stays alive however late the callback runs. Freed by the callback that
// completes the query.
template <typename State>
struct CallbackLease final {
    std::shared_ptr<State> value;
};

void completeBrowse(CallbackLease<BrowseState>* lease, const std::shared_ptr<BrowseState>& state) noexcept
{
    if (!state->completed.exchange(true)) {
        SetEvent(state->completion.get());
        delete lease;
    }
}

VOID WINAPI browseCallback(DWORD status, PVOID context, PDNS_RECORD records)
{
    auto* lease = static_cast<CallbackLease<BrowseState>*>(context);
    const std::shared_ptr<BrowseState> state = lease != nullptr ? lease->value : nullptr;
    if (state && status == ERROR_SUCCESS && records != nullptr) {
        const std::lock_guard guard{state->mutex};
        for (PDNS_RECORD record = records; record != nullptr; record = record->pNext) {
            if (record->wType == DNS_TYPE_PTR && record->Data.PTR.pNameHost != nullptr) {
                state->instances.insert(QString::fromWCharArray(record->Data.PTR.pNameHost));
            }
        }
    }
    if (records != nullptr) {
        DnsRecordListFree(records, DnsFreeRecordList);
    }
    // The browse ends with a non-success status (cancelled or failed).
    if (state && status != ERROR_SUCCESS) {
        completeBrowse(lease, state);
    }
}

[[nodiscard]] std::optional<DnsServiceRecord> readRecord(PDNS_SERVICE_INSTANCE instance)
{
    if (instance == nullptr) {
        return std::nullopt;
    }
    DnsServiceRecord record;
    if (instance->pszInstanceName != nullptr) {
        record.instanceName = QString::fromWCharArray(instance->pszInstanceName);
    }
    if (instance->ip4Address != nullptr) {
        const DWORD ip = *instance->ip4Address;
        record.host = QStringLiteral("%1.%2.%3.%4")
                          .arg(ip & 0xFFU)
                          .arg((ip >> 8U) & 0xFFU)
                          .arg((ip >> 16U) & 0xFFU)
                          .arg((ip >> 24U) & 0xFFU);
    } else if (instance->pszHostName != nullptr) {
        record.host = QString::fromWCharArray(instance->pszHostName);
    }
    record.port = instance->wPort;
    for (DWORD index = 0; index < instance->dwPropertyCount; ++index) {
        if (instance->keys == nullptr || instance->values == nullptr || instance->keys[index] == nullptr) {
            continue;
        }
        const QString key = QString::fromWCharArray(instance->keys[index]).toLower();
        const QString value =
            instance->values[index] != nullptr ? QString::fromWCharArray(instance->values[index]) : QString();
        record.properties.insert(key, value);
    }
    return record;
}

VOID WINAPI resolveCallback(DWORD status, PVOID context, PDNS_SERVICE_INSTANCE instance)
{
    const std::unique_ptr<CallbackLease<ResolveState>> lease{static_cast<CallbackLease<ResolveState>*>(context)};
    const std::shared_ptr<ResolveState> state = lease ? lease->value : nullptr;
    if (state && status == ERROR_SUCCESS) {
        std::optional<DnsServiceRecord> record = readRecord(instance);
        const std::lock_guard guard{state->mutex};
        state->record = std::move(record);
    }
    if (instance != nullptr) {
        DnsServiceFreeInstance(instance);
    }
    if (state && !state->completed.exchange(true)) {
        SetEvent(state->completion.get());
    }
}

class BrowseOperation final : public IDnsBrowseOperation {
public:
    explicit BrowseOperation(std::shared_ptr<BrowseState> state) : m_state(std::move(state)) {}
    ~BrowseOperation() override { cancelAndWait(); }
    BrowseOperation(const BrowseOperation&) = delete;
    BrowseOperation& operator=(const BrowseOperation&) = delete;

    bool wait(int timeoutMs) override
    {
        return WaitForSingleObject(m_state->completion.get(), static_cast<DWORD>(std::max(0, timeoutMs)))
            == WAIT_OBJECT_0;
    }

    void cancelAndWait() noexcept override
    {
        if (m_state->completed.load()) {
            return;
        }
        if (!m_state->cancellationRequested.exchange(true) && DnsServiceBrowseCancel(&m_state->cancel) != ERROR_SUCCESS) {
            return;
        }
        WaitForSingleObject(m_state->completion.get(), CancellationCallbackWaitMs);
    }

    [[nodiscard]] QStringList instances() const override
    {
        const std::lock_guard guard{m_state->mutex};
        QStringList names(m_state->instances.cbegin(), m_state->instances.cend());
        names.sort();
        return names;
    }

private:
    std::shared_ptr<BrowseState> m_state;
};

class ResolveOperation final : public IDnsResolveOperation {
public:
    explicit ResolveOperation(std::shared_ptr<ResolveState> state) : m_state(std::move(state)) {}
    ~ResolveOperation() override { cancelAndWait(); }
    ResolveOperation(const ResolveOperation&) = delete;
    ResolveOperation& operator=(const ResolveOperation&) = delete;

    bool wait(int timeoutMs) override
    {
        return WaitForSingleObject(m_state->completion.get(), static_cast<DWORD>(std::max(0, timeoutMs)))
            == WAIT_OBJECT_0;
    }

    void cancelAndWait() noexcept override
    {
        if (m_state->completed.load()) {
            return;
        }
        if (!m_state->cancellationRequested.exchange(true)
            && DnsServiceResolveCancel(&m_state->cancel) != ERROR_SUCCESS) {
            return;
        }
        WaitForSingleObject(m_state->completion.get(), CancellationCallbackWaitMs);
    }

    [[nodiscard]] std::optional<DnsServiceRecord> record() const override
    {
        const std::lock_guard guard{m_state->mutex};
        return m_state->record;
    }

private:
    std::shared_ptr<ResolveState> m_state;
};

} // namespace

std::unique_ptr<IDnsBrowseOperation> WindowsDnsServiceBrowser::beginBrowse(const QString& serviceName)
{
    auto state = std::make_shared<BrowseState>();
    if (!state->completion) {
        return nullptr;
    }
    state->queryName = serviceName.toStdWString();
    auto* lease = new (std::nothrow) CallbackLease<BrowseState>{state};
    if (lease == nullptr) {
        return nullptr;
    }
    DNS_SERVICE_BROWSE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = state->queryName.c_str();
    request.pBrowseCallback = &browseCallback;
    request.pQueryContext = lease;
    if (DnsServiceBrowse(&request, &state->cancel) != DNS_REQUEST_PENDING) {
        delete lease;
        return nullptr;
    }
    return std::make_unique<BrowseOperation>(std::move(state));
}

std::unique_ptr<IDnsResolveOperation> WindowsDnsServiceBrowser::beginResolve(const QString& instanceName)
{
    auto state = std::make_shared<ResolveState>();
    if (!state->completion) {
        return nullptr;
    }
    state->queryName = instanceName.toStdWString();
    auto* lease = new (std::nothrow) CallbackLease<ResolveState>{state};
    if (lease == nullptr) {
        return nullptr;
    }
    DNS_SERVICE_RESOLVE_REQUEST request{};
    request.Version = DNS_QUERY_REQUEST_VERSION1;
    request.InterfaceIndex = 0;
    request.QueryName = state->queryName.data();
    request.pResolveCompletionCallback = &resolveCallback;
    request.pQueryContext = lease;
    if (DnsServiceResolve(&request, &state->cancel) != DNS_REQUEST_PENDING) {
        delete lease;
        return nullptr;
    }
    return std::make_unique<ResolveOperation>(std::move(state));
}

} // namespace lmp::platform::windows
