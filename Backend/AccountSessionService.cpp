#include "pch.h"
#include "Backend/AccountSessionService.h"

#include "Backend/AccountClient.h"

#include <windows.h>

#include <utility>

namespace LastMusicPlayer::Backend
{
    AccountSessionService::AccountSessionService(
        SettingsManager& settings,
        ICredentialStore& credentials,
        IAccountSessionGateway& gateway,
        UserDataOperationGate& operationGate)
        : m_settings(settings),
          m_credentials(credentials),
          m_gateway(gateway),
          m_operationGate(operationGate)
    {
    }

    AccountSessionService::TransitionLease::TransitionLease(
        AccountSessionService& service)
        : m_service(&service)
    {
        std::unique_lock lock{ service.m_transitionMutex };
        service.m_transitionCondition.wait(lock, [&service]
        {
            return !service.m_transitionInProgress;
        });
        service.m_transitionInProgress = true;
    }

    AccountSessionService::TransitionLease::~TransitionLease()
    {
        Release();
    }

    void AccountSessionService::TransitionLease::Release() noexcept
    {
        if (!m_service)
        {
            return;
        }

        auto* service = std::exchange(m_service, nullptr);
        {
            std::lock_guard guard{ service->m_transitionMutex };
            service->m_transitionInProgress = false;
        }
        service->m_transitionCondition.notify_one();
    }

    bool AccountSessionService::IsAccountIntegrationAvailable() const noexcept
    {
        return m_gateway.IsConfigured();
    }

    AccountSessionSnapshot AccountSessionService::Snapshot() const
    {
        std::lock_guard guard{ m_mutex };
        return { m_status, m_profile, m_lastSafeError, m_generation };
    }

    AccountSessionStatus AccountSessionService::Status() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_status;
    }

    AccountProfile AccountSessionService::CurrentProfile() const
    {
        std::lock_guard guard{ m_mutex };
        return m_profile;
    }

    std::uint64_t AccountSessionService::Generation() const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return m_generation;
    }

    winrt::hstring AccountSessionService::LastSafeError() const
    {
        std::lock_guard guard{ m_mutex };
        return m_lastSafeError;
    }

    std::uint64_t AccountSessionService::BeginAuthorizationIntent() noexcept
    {
        std::uint64_t intent{};
        {
            std::lock_guard guard{ m_mutex };
            intent = ++m_authorizationIntent;
        }
        m_restoreCondition.notify_all();
        m_gateway.CancelAcquireBearerSession(intent);
        return intent;
    }

    bool AccountSessionService::IsAuthorizationIntentCurrent(std::uint64_t intent) const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return intent == m_authorizationIntent;
    }

    void AccountSessionService::SetSafeErrorIfCurrent(
        std::uint64_t intent,
        winrt::hstring const& message) noexcept
    {
        try
        {
            std::lock_guard guard{ m_mutex };
            if (intent == m_authorizationIntent)
            {
                m_lastSafeError = message;
            }
        }
        catch (...)
        {
        }
    }

    AccountProfile AccountSessionService::ProfileHints() const
    {
        AccountProfile profile;
        profile.Id = m_settings.GetString(L"AccountOwnerHint", L"");
        profile.DisplayName = m_settings.GetString(L"AccountDisplayNameHint", L"");
        profile.Username = m_settings.GetString(L"AccountUsernameHint", L"");
        profile.PlanLabel = m_settings.GetString(L"AccountPlanHint", L"");
        return profile;
    }

    void AccountSessionService::PersistProfileHints(AccountProfile const& profile)
    {
        m_settings.SetString(L"AccountOwnerHint", profile.Id);
        m_settings.SetString(L"AccountDisplayNameHint", profile.DisplayName);
        m_settings.SetString(L"AccountUsernameHint", profile.Username);
        m_settings.SetString(L"AccountPlanHint", profile.PlanLabel);
    }

    AccountSessionService::StateEffect AccountSessionService::PublishStateLocked(
        AccountSessionStatus status,
        AccountProfile const& profile,
        winrt::hstring const& bearerSession,
        winrt::hstring const& safeError,
        bool persistProfile)
    {
        StateEffect effect;
        auto previousOwner = m_profile.Id;
        ++m_generation;
        m_status = status;
        m_profile = profile;
        m_bearerSession = bearerSession;
        m_lastSafeError = safeError;

        effect.PersistProfile = persistProfile && !profile.Id.empty();
        effect.Profile = profile;
        effect.NotifyOwner = previousOwner != profile.Id;
        effect.OwnerId = profile.Id;
        if (effect.NotifyOwner)
        {
            try
            {
                effect.OwnerChanged = m_ownerChanged;
            }
            catch (...)
            {
                effect.NotifyOwner = false;
            }
        }
        return effect;
    }

    void AccountSessionService::EnqueueEffect(StateEffect effect)
    {
        if (!effect.PersistProfile && !effect.NotifyOwner)
        {
            return;
        }
        std::lock_guard guard{ m_effectMutex };
        m_effects.push_back(std::move(effect));
    }

    void AccountSessionService::DrainEffects() noexcept
    {
        {
            std::lock_guard guard{ m_effectMutex };
            if (m_drainingEffects)
            {
                return;
            }
            m_drainingEffects = true;
        }

        for (;;)
        {
            StateEffect effect;
            {
                std::lock_guard guard{ m_effectMutex };
                if (m_effects.empty())
                {
                    m_drainingEffects = false;
                    return;
                }
                effect = std::move(m_effects.front());
                m_effects.pop_front();
            }

            if (effect.PersistProfile)
            {
                try
                {
                    PersistProfileHints(effect.Profile);
                }
                catch (...)
                {
                }
            }
            if (effect.NotifyOwner && effect.OwnerChanged)
            {
                try
                {
                    effect.OwnerChanged(effect.OwnerId);
                }
                catch (...)
                {
                }
            }
        }
    }

    bool AccountSessionService::PublishSigningIn(std::uint64_t intent)
    {
        TransitionLease transition{ *this };
        std::lock_guard guard{ m_mutex };
        if (intent != m_authorizationIntent)
        {
            return false;
        }

        ++m_generation;
        m_status = AccountSessionStatus::SigningIn;
        m_lastSafeError = {};
        return true;
    }

    bool AccountSessionService::CommitValidated(
        std::uint64_t intent,
        AccountProfile const& profile,
        winrt::hstring const& bearerSession,
        bool storeCredential)
    {
        if (profile.Id.empty() || bearerSession.empty())
        {
            return false;
        }

        TransitionLease transition{ *this };
        if (!IsAuthorizationIntentCurrent(intent))
        {
            return false;
        }

        bool wroteCredential{};
        if (storeCredential)
        {
            wroteCredential = m_credentials.WriteAccountSession(bearerSession);
            if (!wroteCredential || m_credentials.ReadAccountSession() != bearerSession)
            {
                if (wroteCredential)
                {
                    (void)m_credentials.DeleteAccountSessionIfMatches(bearerSession);
                }
                return false;
            }
        }
        else if (m_credentials.ReadAccountSession() != bearerSession)
        {
            return false;
        }

#if defined(LAST_MUSIC_NATIVE_ACCOUNT_TESTS)
        if (m_beforeValidatedStateCommitForTesting)
        {
            m_beforeValidatedStateCommitForTesting();
        }
#endif

        StateEffect effect;
        bool published{};
        {
            std::lock_guard guard{ m_mutex };
            if (intent == m_authorizationIntent)
            {
                effect = PublishStateLocked(
                    AccountSessionStatus::Validated,
                    profile,
                    bearerSession,
                    {},
                    true);
                published = true;
            }
        }
        if (!published)
        {
            if (wroteCredential)
            {
                (void)m_credentials.DeleteAccountSessionIfMatches(bearerSession);
            }
            return false;
        }
        EnqueueEffect(std::move(effect));
        transition.Release();
        DrainEffects();
        return true;
    }

    bool AccountSessionService::CommitOffline(
        std::uint64_t intent,
        AccountProfile const& profile,
        winrt::hstring const& bearerSession)
    {
        if (profile.Id.empty() || bearerSession.empty())
        {
            return false;
        }

        TransitionLease transition{ *this };
        if (!IsAuthorizationIntentCurrent(intent)
            || m_credentials.ReadAccountSession() != bearerSession)
        {
            return false;
        }

        StateEffect effect;
        {
            std::lock_guard guard{ m_mutex };
            if (intent != m_authorizationIntent)
            {
                return false;
            }
            effect = PublishStateLocked(
                AccountSessionStatus::Offline,
                profile,
                bearerSession,
                L"Account service unavailable. Using the offline library.",
                false);
        }
        EnqueueEffect(std::move(effect));
        transition.Release();
        DrainEffects();
        return true;
    }

    bool AccountSessionService::CommitSignedOut(
        std::uint64_t intent,
        CredentialRemoval credentialRemoval,
        winrt::hstring const& expectedSession,
        winrt::hstring const& safeError)
    {
        TransitionLease transition{ *this };
        if (!IsAuthorizationIntentCurrent(intent))
        {
            return false;
        }

        bool credentialRemoved = true;
        switch (credentialRemoval)
        {
        case CredentialRemoval::DeleteAll:
            credentialRemoved = m_credentials.DeleteAccountSession();
            break;
        case CredentialRemoval::DeleteIfMatches:
        {
            auto result = m_credentials.DeleteAccountSessionIfMatches(expectedSession);
            credentialRemoved = result == AccountCredentialDeleteResult::Deleted
                || result == AccountCredentialDeleteResult::NotFound;
            break;
        }
        case CredentialRemoval::Keep:
            break;
        }
        if (!credentialRemoved)
        {
            return false;
        }

        StateEffect effect;
        {
            std::lock_guard guard{ m_mutex };
            if (intent != m_authorizationIntent)
            {
                return false;
            }
            effect = PublishStateLocked(
                AccountSessionStatus::SignedOut,
                {},
                {},
                safeError,
                false);
        }
        EnqueueEffect(std::move(effect));
        transition.Release();
        DrainEffects();
        return true;
    }

    winrt::Windows::Foundation::IAsyncAction AccountSessionService::RestoreAsync()
    {
        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            co_return;
        }
        auto intent = BeginAuthorizationIntent();

        co_await winrt::resume_background();
        if (!IsAuthorizationIntentCurrent(intent))
        {
            co_return;
        }

        {
            std::unique_lock lock{ m_mutex };
            if (m_restoreInFlight)
            {
                m_restoreCondition.wait(lock, [this, intent]
                {
                    return !m_restoreInFlight || intent != m_authorizationIntent;
                });
                if (intent != m_authorizationIntent)
                {
                    co_return;
                }
            }
            m_restoreInFlight = true;
        }

        struct RestoreCompletion
        {
            AccountSessionService* Service;
            ~RestoreCompletion()
            {
                {
                    std::lock_guard guard{ Service->m_mutex };
                    Service->m_restoreInFlight = false;
                }
                Service->m_restoreCondition.notify_all();
            }
        } completion{ this };

        auto bearer = m_credentials.ReadAccountSession();
        if (!IsAuthorizationIntentCurrent(intent))
        {
            co_return;
        }
        if (bearer.empty())
        {
            (void)CommitSignedOut(intent, CredentialRemoval::Keep, {});
            co_return;
        }
        if (!m_gateway.IsConfigured())
        {
            auto profile = ProfileHints();
            if (profile.Id.empty())
            {
                (void)CommitSignedOut(intent, CredentialRemoval::Keep, {});
            }
            else
            {
                (void)CommitOffline(intent, profile, bearer);
            }
            co_return;
        }

        try
        {
            AccountProfile profile;
            co_await m_gateway.GetProfileAsync(
                bearer,
                [&profile](AccountProfile const& value)
                {
                    profile = value;
                });
            if (!IsAuthorizationIntentCurrent(intent))
            {
                co_return;
            }
            if (!CommitValidated(intent, profile, bearer, false)
                && IsAuthorizationIntentCurrent(intent))
            {
                (void)CommitSignedOut(intent, CredentialRemoval::Keep, {});
            }
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsAuthorizationIntentCurrent(intent))
            {
                co_return;
            }
            if (IsAccountUnauthorized(error) || error.code() == E_ACCESSDENIED)
            {
                (void)CommitSignedOut(
                    intent,
                    CredentialRemoval::DeleteIfMatches,
                    bearer);
                co_return;
            }

            auto profile = ProfileHints();
            if (profile.Id.empty())
            {
                (void)CommitSignedOut(intent, CredentialRemoval::Keep, {});
            }
            else
            {
                (void)CommitOffline(intent, profile, bearer);
            }
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> AccountSessionService::SignInAsync()
    {
        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            co_return false;
        }
        auto intent = BeginAuthorizationIntent();
        if (!m_gateway.IsConfigured())
        {
            SetSafeErrorIfCurrent(intent, L"Account integration is unavailable in this build.");
            co_return false;
        }
        if (!PublishSigningIn(intent))
        {
            co_return false;
        }

        try
        {
            auto bearer = co_await m_gateway.AcquireBearerSessionAsync(intent);
            if (!IsAuthorizationIntentCurrent(intent))
            {
                co_return false;
            }

            AccountProfile profile;
            co_await m_gateway.GetProfileAsync(
                bearer,
                [&profile](AccountProfile const& value)
                {
                    profile = value;
                });
            if (!IsAuthorizationIntentCurrent(intent))
            {
                co_return false;
            }
            if (CommitValidated(intent, profile, bearer, true))
            {
                co_return true;
            }
            if (IsAuthorizationIntentCurrent(intent))
            {
                (void)CommitSignedOut(
                    intent,
                    CredentialRemoval::Keep,
                    {},
                    L"Could not store the account session securely.");
            }
        }
        catch (winrt::hresult_canceled const&)
        {
            if (IsAuthorizationIntentCurrent(intent))
            {
                (void)CommitSignedOut(
                    intent,
                    CredentialRemoval::Keep,
                    {},
                    L"Account sign-in was canceled.");
            }
        }
        catch (winrt::hresult_error const& error)
        {
            if (IsAuthorizationIntentCurrent(intent))
            {
                auto message = error.message();
                (void)CommitSignedOut(
                    intent,
                    CredentialRemoval::Keep,
                    {},
                    message.empty()
                        ? winrt::hstring{ L"Account sign-in failed." }
                        : message);
            }
        }
        catch (...)
        {
            if (IsAuthorizationIntentCurrent(intent))
            {
                (void)CommitSignedOut(
                    intent,
                    CredentialRemoval::Keep,
                    {},
                    L"Account sign-in failed.");
            }
        }
        co_return false;
    }

    void AccountSessionService::CancelSignIn() noexcept
    {
        std::uint64_t intent{};
        {
            std::lock_guard guard{ m_mutex };
            intent = m_authorizationIntent;
        }
        m_gateway.CancelAcquireBearerSession(intent);
    }

    winrt::Windows::Foundation::IAsyncOperation<bool> AccountSessionService::LogoutAsync()
    {
        auto operationLease = m_operationGate.TryEnter();
        if (!operationLease)
        {
            co_return false;
        }
        auto intent = BeginAuthorizationIntent();

        AccountOperationContext context;
        try
        {
            context = CaptureOperation();
        }
        catch (...)
        {
            co_return CommitSignedOut(intent, CredentialRemoval::DeleteAll, {});
        }

        try
        {
            co_await m_gateway.LogoutAsync(context.BearerSession);
            if (!IsAuthorizationIntentCurrent(intent) || !IsCurrent(context))
            {
                co_return false;
            }
            co_return CommitSignedOut(
                intent,
                CredentialRemoval::DeleteIfMatches,
                context.BearerSession);
        }
        catch (winrt::hresult_error const& error)
        {
            if (!IsAuthorizationIntentCurrent(intent) || !IsCurrent(context))
            {
                co_return false;
            }
            if (IsAccountUnauthorized(error))
            {
                co_return CommitSignedOut(
                    intent,
                    CredentialRemoval::DeleteIfMatches,
                    context.BearerSession);
            }
            SetSafeErrorIfCurrent(intent, L"Could not contact the account service.");
            co_return false;
        }
    }

    bool AccountSessionService::RemoveLocalSession() noexcept
    {
        try
        {
            auto intent = BeginAuthorizationIntent();
            return CommitSignedOut(intent, CredentialRemoval::DeleteAll, {});
        }
        catch (...)
        {
            return false;
        }
    }

    AccountOperationContext AccountSessionService::CaptureOperation() const
    {
        std::lock_guard guard{ m_mutex };
        if ((m_status != AccountSessionStatus::Validated && m_status != AccountSessionStatus::Offline)
            || m_profile.Id.empty() || m_bearerSession.empty())
        {
            throw winrt::hresult_error(E_NOT_VALID_STATE, L"Sign in to use Account mode.");
        }
        return { m_profile.Id, m_bearerSession, m_status, m_generation };
    }

    bool AccountSessionService::IsCurrent(AccountOperationContext const& context) const noexcept
    {
        std::lock_guard guard{ m_mutex };
        return context.Generation == m_generation
            && context.OwnerId == m_profile.Id
            && context.BearerSession == m_bearerSession
            && context.Status == m_status
            && (m_status == AccountSessionStatus::Validated || m_status == AccountSessionStatus::Offline);
    }

    void AccountSessionService::HandleUnauthorized(
        AccountOperationContext const& context) noexcept
    {
        try
        {
            TransitionLease transition{ *this };
            std::uint64_t intent{};
            {
                std::lock_guard guard{ m_mutex };
                if (context.Generation != m_generation
                    || context.OwnerId != m_profile.Id
                    || context.BearerSession != m_bearerSession
                    || context.Status != m_status
                    || (m_status != AccountSessionStatus::Validated
                        && m_status != AccountSessionStatus::Offline))
                {
                    return;
                }
                intent = ++m_authorizationIntent;
            }

            auto result = m_credentials.DeleteAccountSessionIfMatches(context.BearerSession);
            if (result != AccountCredentialDeleteResult::Deleted
                && result != AccountCredentialDeleteResult::NotFound)
            {
                transition.Release();
                m_gateway.CancelAcquireBearerSession(intent);
                return;
            }

            StateEffect effect;
            bool published{};
            {
                std::lock_guard guard{ m_mutex };
                if (intent == m_authorizationIntent
                    && context.Generation == m_generation
                    && context.OwnerId == m_profile.Id
                    && context.BearerSession == m_bearerSession
                    && context.Status == m_status)
                {
                    effect = PublishStateLocked(
                        AccountSessionStatus::SignedOut,
                        {},
                        {},
                        {},
                        false);
                    published = true;
                }
            }
            if (!published)
            {
                transition.Release();
                m_gateway.CancelAcquireBearerSession(intent);
                return;
            }
            EnqueueEffect(std::move(effect));
            transition.Release();
            m_gateway.CancelAcquireBearerSession(intent);
            DrainEffects();
        }
        catch (...)
        {
        }
    }

    void AccountSessionService::SetOwnerChangedCallback(
        std::function<void(winrt::hstring const&)> callback)
    {
        std::lock_guard guard{ m_mutex };
        m_ownerChanged = std::move(callback);
    }

#if defined(LAST_MUSIC_NATIVE_ACCOUNT_TESTS)
    void AccountSessionService::SetBeforeValidatedStateCommitForTesting(
        std::function<void()> callback)
    {
        TransitionLease transition{ *this };
        m_beforeValidatedStateCommitForTesting = std::move(callback);
    }
#endif
}
