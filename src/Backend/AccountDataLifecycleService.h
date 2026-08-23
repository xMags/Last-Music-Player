#pragma once

#include "Backend/AccountDataLifecycleContracts.h"
#include "Backend/AccountSessionService.h"
#include "Backend/SettingsManager.h"
#include "Backend/UserDataOperationGate.h"

#include <memory>
#include <mutex>
#include <utility>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    inline constexpr wchar_t PendingAccountCleanupOwnerSetting[] =
        L"PendingAccountCleanupOwnerId";

    enum class AccountLogoutOutcome
    {
        Succeeded,
        NoAuthenticatedAccount,
        FinalSyncFailed,
        CleanupBusy,
        CleanupTimedOut,
        SessionChanged,
        MarkerWriteFailed,
        RemoteLogoutFailed,
        CleanupDeferred,
        UnexpectedFailure,
    };

    struct AccountLogoutResult
    {
        AccountLogoutOutcome Outcome{ AccountLogoutOutcome::UnexpectedFailure };
        bool SignedOut{};
        winrt::hstring SafeError;
    };

    enum class AccountCleanupOutcome
    {
        NothingPending,
        PreservedAuthenticatedAccount,
        Cleaned,
        InvalidMarkerRemoved,
        CleanupBusy,
        CleanupTimedOut,
        MarkerWriteFailed,
        CleanupDeferred,
        UnexpectedFailure,
    };

    struct AccountCleanupResult
    {
        AccountCleanupOutcome Outcome{ AccountCleanupOutcome::UnexpectedFailure };
        winrt::hstring SafeError;
    };

    template <typename TResult>
    class AccountLifecycleOperation final
    {
    public:
        AccountLifecycleOperation()
            : m_state(std::make_shared<State>())
        {
        }

        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction Completion() const
        {
            std::lock_guard guard{ m_state->Mutex };
            return m_state->Completion;
        }

        [[nodiscard]] TResult Result() const
        {
            std::lock_guard guard{ m_state->Mutex };
            return m_state->Result;
        }

    private:
        friend class AccountDataLifecycleService;

        struct State final
        {
            mutable std::mutex Mutex;
            TResult Result;
            winrt::Windows::Foundation::IAsyncAction Completion{ nullptr };
        };

        void SetCompletion(winrt::Windows::Foundation::IAsyncAction completion)
        {
            std::lock_guard guard{ m_state->Mutex };
            m_state->Completion = std::move(completion);
        }

        void SetResult(TResult result)
        {
            std::lock_guard guard{ m_state->Mutex };
            m_state->Result = std::move(result);
        }

        std::shared_ptr<State> m_state;
    };

    using AccountLogoutOperation = AccountLifecycleOperation<AccountLogoutResult>;
    using AccountCleanupOperation = AccountLifecycleOperation<AccountCleanupResult>;

    class AccountDataLifecycleService final
    {
    public:
        AccountDataLifecycleService(
            AccountSessionService& session,
            IAccountOutboundSync& outboundSync,
            IAccountDataLifecycleStorage& storage,
            SettingsManager& settings,
            UserDataOperationGate& operationGate);

        [[nodiscard]] AccountLogoutOperation Logout();
        [[nodiscard]] AccountCleanupOperation ResolvePendingCleanup();

    private:
        winrt::Windows::Foundation::IAsyncAction LogoutCoreAsync(
            AccountLogoutOperation operation);
        winrt::Windows::Foundation::IAsyncAction ResolvePendingCleanupCoreAsync(
            AccountCleanupOperation operation);

        AccountSessionService& m_session;
        IAccountOutboundSync& m_outboundSync;
        IAccountDataLifecycleStorage& m_storage;
        SettingsManager& m_settings;
        UserDataOperationGate& m_operationGate;
    };
}
