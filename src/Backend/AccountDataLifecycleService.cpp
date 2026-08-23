#include "pch.h"
#include "Backend/AccountDataLifecycleService.h"

#include <algorithm>
#include <chrono>
#include <string>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        constexpr auto CleanupWaitTimeout = std::chrono::seconds(30);
        constexpr std::size_t MaximumOwnerIdLength = 512;

        class AdmissionClosure final
        {
        public:
            explicit AdmissionClosure(UserDataOperationGate& gate) noexcept
                : m_gate(&gate)
            {
            }

            AdmissionClosure(AdmissionClosure const&) = delete;
            AdmissionClosure& operator=(AdmissionClosure const&) = delete;

            ~AdmissionClosure()
            {
                if (m_gate)
                {
                    m_gate->Reopen();
                }
            }

        private:
            UserDataOperationGate* m_gate;
        };

        [[nodiscard]] bool IsAuthenticated(AccountSessionSnapshot const& snapshot) noexcept
        {
            return (snapshot.Status == AccountSessionStatus::Validated
                    || snapshot.Status == AccountSessionStatus::Offline)
                && !snapshot.Profile.Id.empty();
        }

        [[nodiscard]] bool IsValidOwnerId(std::wstring const& ownerId) noexcept
        {
            if (ownerId.empty() || ownerId.size() > MaximumOwnerIdLength)
            {
                return false;
            }
            return std::none_of(ownerId.begin(), ownerId.end(), [](wchar_t character)
            {
                return character < L' ';
            });
        }
    }

    AccountDataLifecycleService::AccountDataLifecycleService(
        AccountSessionService& session,
        IAccountOutboundSync& outboundSync,
        IAccountDataLifecycleStorage& storage,
        SettingsManager& settings,
        UserDataOperationGate& operationGate)
        : m_session(session),
          m_outboundSync(outboundSync),
          m_storage(storage),
          m_settings(settings),
          m_operationGate(operationGate)
    {
    }

    AccountLogoutOperation AccountDataLifecycleService::Logout()
    {
        AccountLogoutOperation operation;
        operation.SetCompletion(LogoutCoreAsync(operation));
        return operation;
    }

    AccountCleanupOperation AccountDataLifecycleService::ResolvePendingCleanup()
    {
        AccountCleanupOperation operation;
        operation.SetCompletion(ResolvePendingCleanupCoreAsync(operation));
        return operation;
    }

    winrt::Windows::Foundation::IAsyncAction AccountDataLifecycleService::LogoutCoreAsync(
        AccountLogoutOperation operation)
    {
        try
        {
            AccountOperationContext context;
            try
            {
                context = m_session.CaptureOperation();
            }
            catch (...)
            {
                operation.SetResult({
                    AccountLogoutOutcome::NoAuthenticatedAccount,
                    false,
                    L"No signed-in account is available."
                });
                co_return;
            }

            if (!m_storage.IsAccountDataStoreReady())
            {
                operation.SetResult({
                    AccountLogoutOutcome::CleanupDeferred,
                    false,
                    L"Account data is not available yet. Try signing out again shortly."
                });
                co_return;
            }

            auto const synchronized = co_await m_outboundSync.RequestOutboundSyncAsync();
            if (!m_session.IsCurrent(context))
            {
                auto const snapshot = m_session.Snapshot();
                if (snapshot.Status == AccountSessionStatus::SignedOut
                    && snapshot.Profile.Id.empty())
                {
                    operation.SetResult({
                        AccountLogoutOutcome::FinalSyncFailed,
                        true,
                        L"The account session expired during final sync. Local account data was preserved."
                    });
                    co_return;
                }
                operation.SetResult({
                    AccountLogoutOutcome::SessionChanged,
                    false,
                    L"The active account changed. Nothing was removed."
                });
                co_return;
            }

            auto const ownerId = std::wstring(context.OwnerId.c_str());
            auto const recoverablePending = m_storage.RecoverablePendingChangeCount(ownerId);
            if (!recoverablePending)
            {
                operation.SetResult({
                    AccountLogoutOutcome::FinalSyncFailed,
                    false,
                    L"Pending account changes could not be verified. The account remains signed in."
                });
                co_return;
            }
            if (!synchronized && *recoverablePending > 0)
            {
                operation.SetResult({
                    AccountLogoutOutcome::FinalSyncFailed,
                    false,
                    L"Pending account changes could not be synchronized. The account remains signed in."
                });
                co_return;
            }
            if (*recoverablePending > 0)
            {
                operation.SetResult({
                    AccountLogoutOutcome::FinalSyncFailed,
                    false,
                    L"Some account changes are still waiting to synchronize. The account remains signed in."
                });
                co_return;
            }

            if (!m_operationGate.CloseAdmissions())
            {
                operation.SetResult({
                    AccountLogoutOutcome::CleanupBusy,
                    false,
                    L"Another cleanup operation is already in progress."
                });
                co_return;
            }
            AdmissionClosure admissionClosure{ m_operationGate };

            co_await winrt::resume_background();
            if (!m_operationGate.WaitForIdle(CleanupWaitTimeout))
            {
                operation.SetResult({
                    AccountLogoutOutcome::CleanupTimedOut,
                    false,
                    L"A background data operation did not finish. The account remains signed in."
                });
                co_return;
            }
            if (!m_session.IsCurrent(context))
            {
                operation.SetResult({
                    AccountLogoutOutcome::SessionChanged,
                    false,
                    L"The active account changed. Nothing was removed."
                });
                co_return;
            }

            // Admissions are now closed and every earlier lease has drained,
            // so this second count is stable. A write that landed after the
            // first sync must stop logout rather than be silently discarded.
            auto const stablePending = m_storage.RecoverablePendingChangeCount(ownerId);
            if (!stablePending || *stablePending > 0)
            {
                operation.SetResult({
                    AccountLogoutOutcome::FinalSyncFailed,
                    false,
                    L"A new account change arrived during sign out. The account remains signed in."
                });
                co_return;
            }

            if (!m_settings.TrySetString(PendingAccountCleanupOwnerSetting, context.OwnerId))
            {
                operation.SetResult({
                    AccountLogoutOutcome::MarkerWriteFailed,
                    false,
                    L"Could not prepare safe account cleanup. The account remains signed in."
                });
                co_return;
            }

            auto const signedOut = co_await m_session.LogoutForLifecycleAsync(context);
            if (!signedOut)
            {
                (void)m_settings.Remove(PendingAccountCleanupOwnerSetting);
                operation.SetResult({
                    AccountLogoutOutcome::RemoteLogoutFailed,
                    false,
                    L"Could not complete sign out. The account and its local data were preserved."
                });
                co_return;
            }

            if (!m_storage.RemoveSignedOutAccountData(ownerId))
            {
                operation.SetResult({
                    AccountLogoutOutcome::CleanupDeferred,
                    true,
                    L"Signed out. Local account cleanup will retry automatically next time the app starts."
                });
                co_return;
            }
            if (!m_settings.Remove(PendingAccountCleanupOwnerSetting))
            {
                operation.SetResult({
                    AccountLogoutOutcome::CleanupDeferred,
                    true,
                    L"Signed out. Cleanup was completed, but its recovery marker will be checked again at startup."
                });
                co_return;
            }

            operation.SetResult({ AccountLogoutOutcome::Succeeded, true, {} });
        }
        catch (...)
        {
            operation.SetResult({
                AccountLogoutOutcome::UnexpectedFailure,
                false,
                L"Could not safely complete account sign out."
            });
        }
    }

    winrt::Windows::Foundation::IAsyncAction
        AccountDataLifecycleService::ResolvePendingCleanupCoreAsync(
            AccountCleanupOperation operation)
    {
        try
        {
            auto const marker = m_settings.GetString(PendingAccountCleanupOwnerSetting, L"");
            if (marker.empty())
            {
                operation.SetResult({ AccountCleanupOutcome::NothingPending, {} });
                co_return;
            }

            auto const ownerId = std::wstring(marker.c_str());
            if (!IsValidOwnerId(ownerId))
            {
                auto const removed = m_settings.Remove(PendingAccountCleanupOwnerSetting);
                operation.SetResult(removed
                    ? AccountCleanupResult{ AccountCleanupOutcome::InvalidMarkerRemoved, {} }
                    : AccountCleanupResult{
                        AccountCleanupOutcome::MarkerWriteFailed,
                        L"A pending account cleanup marker could not be repaired."
                    });
                co_return;
            }

            auto snapshot = m_session.Snapshot();
            if (IsAuthenticated(snapshot) && snapshot.Profile.Id == marker)
            {
                auto const removed = m_settings.Remove(PendingAccountCleanupOwnerSetting);
                operation.SetResult(removed
                    ? AccountCleanupResult{
                        AccountCleanupOutcome::PreservedAuthenticatedAccount,
                        {}
                    }
                    : AccountCleanupResult{
                        AccountCleanupOutcome::MarkerWriteFailed,
                        L"The signed-in account was preserved, but cleanup recovery could not be finalized."
                    });
                co_return;
            }

            if (!m_storage.IsAccountDataStoreReady())
            {
                operation.SetResult({
                    AccountCleanupOutcome::CleanupDeferred,
                    L"Pending account cleanup will retry when local data is available."
                });
                co_return;
            }
            if (!m_operationGate.CloseAdmissions())
            {
                operation.SetResult({
                    AccountCleanupOutcome::CleanupBusy,
                    L"Pending account cleanup is already in progress."
                });
                co_return;
            }
            AdmissionClosure admissionClosure{ m_operationGate };

            co_await winrt::resume_background();
            if (!m_operationGate.WaitForIdle(CleanupWaitTimeout))
            {
                operation.SetResult({
                    AccountCleanupOutcome::CleanupTimedOut,
                    L"Pending account cleanup is waiting for a background data operation."
                });
                co_return;
            }

            snapshot = m_session.Snapshot();
            if (IsAuthenticated(snapshot) && snapshot.Profile.Id == marker)
            {
                auto const removed = m_settings.Remove(PendingAccountCleanupOwnerSetting);
                operation.SetResult(removed
                    ? AccountCleanupResult{
                        AccountCleanupOutcome::PreservedAuthenticatedAccount,
                        {}
                    }
                    : AccountCleanupResult{
                        AccountCleanupOutcome::MarkerWriteFailed,
                        L"The signed-in account was preserved, but cleanup recovery could not be finalized."
                    });
                co_return;
            }

            if (!m_storage.RemoveSignedOutAccountData(ownerId))
            {
                operation.SetResult({
                    AccountCleanupOutcome::CleanupDeferred,
                    L"Pending account cleanup will retry automatically next time the app starts."
                });
                co_return;
            }
            if (!m_settings.Remove(PendingAccountCleanupOwnerSetting))
            {
                operation.SetResult({
                    AccountCleanupOutcome::MarkerWriteFailed,
                    L"Account data was cleaned, but its recovery marker could not be removed."
                });
                co_return;
            }

            operation.SetResult({ AccountCleanupOutcome::Cleaned, {} });
        }
        catch (...)
        {
            operation.SetResult({
                AccountCleanupOutcome::UnexpectedFailure,
                L"Pending account cleanup could not be completed."
            });
        }
    }
}
