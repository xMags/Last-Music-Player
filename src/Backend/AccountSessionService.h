#pragma once

#include "Backend/AccountSessionGateway.h"
#include "Backend/CredentialStore.h"
#include "Backend/SettingsManager.h"
#include "Backend/UserDataOperationGate.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    enum class AccountSessionStatus
    {
        SignedOut,
        SigningIn,
        Validated,
        Offline,
    };

    struct AccountSessionSnapshot
    {
        AccountSessionStatus Status{ AccountSessionStatus::SignedOut };
        AccountProfile Profile;
        winrt::hstring LastSafeError;
        std::uint64_t Generation{};
    };

    struct AccountOperationContext
    {
        winrt::hstring OwnerId;
        winrt::hstring BearerSession;
        AccountSessionStatus Status{ AccountSessionStatus::SignedOut };
        std::uint64_t Generation{};
    };

    class AccountSessionService final
    {
    public:
        AccountSessionService(
            SettingsManager& settings,
            ICredentialStore& credentials,
            IAccountSessionGateway& gateway,
            UserDataOperationGate& operationGate);

        bool IsAccountIntegrationAvailable() const noexcept;
        AccountSessionSnapshot Snapshot() const;
        AccountSessionStatus Status() const noexcept;
        AccountProfile CurrentProfile() const;
        std::uint64_t Generation() const noexcept;
        winrt::hstring LastSafeError() const;

        winrt::Windows::Foundation::IAsyncAction RestoreAsync();
        winrt::Windows::Foundation::IAsyncOperation<bool> SignInAsync();
        void CancelSignIn() noexcept;
        winrt::Windows::Foundation::IAsyncOperation<bool> LogoutAsync();
        bool RemoveLocalSession() noexcept;

        AccountOperationContext CaptureOperation() const;
        bool IsCurrent(AccountOperationContext const& context) const noexcept;
        void HandleUnauthorized(AccountOperationContext const& context) noexcept;
        void SetOwnerChangedCallback(std::function<void(winrt::hstring const&)> callback);
#if defined(LAST_MUSIC_NATIVE_ACCOUNT_TESTS)
        void SetBeforeValidatedStateCommitForTesting(std::function<void()> callback);
#endif

    private:
        class TransitionLease final
        {
        public:
            explicit TransitionLease(AccountSessionService& service);
            ~TransitionLease();

            TransitionLease(TransitionLease const&) = delete;
            TransitionLease& operator=(TransitionLease const&) = delete;

            void Release() noexcept;

        private:
            AccountSessionService* m_service;
        };

        enum class CredentialRemoval
        {
            Keep,
            DeleteAll,
            DeleteIfMatches,
        };

        struct StateEffect
        {
            bool PersistProfile{};
            AccountProfile Profile;
            bool NotifyOwner{};
            winrt::hstring OwnerId;
            std::function<void(winrt::hstring const&)> OwnerChanged;
        };

        std::uint64_t BeginAuthorizationIntent() noexcept;
        bool IsAuthorizationIntentCurrent(std::uint64_t intent) const noexcept;
        bool PublishSigningIn(std::uint64_t intent);
        bool CommitValidated(
            std::uint64_t intent,
            AccountProfile const& profile,
            winrt::hstring const& bearerSession,
            bool storeCredential);
        bool CommitOffline(
            std::uint64_t intent,
            AccountProfile const& profile,
            winrt::hstring const& bearerSession);
        bool CommitSignedOut(
            std::uint64_t intent,
            CredentialRemoval credentialRemoval,
            winrt::hstring const& expectedSession,
            winrt::hstring const& safeError = {});
        StateEffect PublishStateLocked(
            AccountSessionStatus status,
            AccountProfile const& profile,
            winrt::hstring const& bearerSession,
            winrt::hstring const& safeError,
            bool persistProfile);
        void EnqueueEffect(StateEffect effect);
        void DrainEffects() noexcept;
        void SetSafeErrorIfCurrent(
            std::uint64_t intent,
            winrt::hstring const& message) noexcept;
        AccountProfile ProfileHints() const;
        void PersistProfileHints(AccountProfile const& profile);

        SettingsManager& m_settings;
        ICredentialStore& m_credentials;
        IAccountSessionGateway& m_gateway;
        UserDataOperationGate& m_operationGate;

        mutable std::mutex m_mutex;
        // Credential mutation and state publication remain one logical transition,
        // but no mutex is held while the synchronous credential API is running.
        std::mutex m_transitionMutex;
        std::condition_variable m_transitionCondition;
        bool m_transitionInProgress{};
        std::condition_variable m_restoreCondition;
        bool m_restoreInFlight{};
        AccountSessionStatus m_status{ AccountSessionStatus::SignedOut };
        AccountProfile m_profile;
        winrt::hstring m_bearerSession;
        winrt::hstring m_lastSafeError;
        std::uint64_t m_generation{ 1 };
        std::uint64_t m_authorizationIntent{ 1 };
        std::function<void(winrt::hstring const&)> m_ownerChanged;
#if defined(LAST_MUSIC_NATIVE_ACCOUNT_TESTS)
        std::function<void()> m_beforeValidatedStateCommitForTesting;
#endif

        std::mutex m_effectMutex;
        std::deque<StateEffect> m_effects;
        bool m_drainingEffects{};
    };
}
