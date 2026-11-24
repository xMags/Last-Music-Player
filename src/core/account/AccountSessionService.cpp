#include "core/account/AccountSessionService.h"

#include "core/account/AccountClient.h"
#include "core/account/SecretStore.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <utility>

namespace lmp::core::account {

namespace keys = settings::keys;

AccountSessionService::AccountSessionService(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_client(dependencies.client)
    , m_secrets(dependencies.secrets)
    , m_settings(dependencies.settings)
    , m_links(dependencies.links)
    , m_operationGate(dependencies.operationGate)
{
}

bool AccountSessionService::isAvailable() const
{
    return m_client.isConfigured();
}

AccountSessionService::Token AccountSessionService::beginIntent()
{
    // The token moves first, so anything the cancellations below finish
    // already sees itself as stale.
    const Token token = m_intents.next();
    std::optional<SignInAttempt> superseded = std::exchange(m_signIn, std::nullopt);
    m_callback.cancel();
    if (superseded) {
        [[maybe_unused]] const bool stored = superseded->promise.addResult(
            Result<void>(Failure::of(FailureKind::Cancelled, QStringLiteral("sign-in was superseded"))));
        superseded->promise.finish();
    }
    return token;
}

void AccountSessionService::restore()
{
    std::optional<UserDataOperationGate::Lease> lease = m_operationGate.tryEnter();
    if (!lease) {
        return;
    }
    const Token token = beginIntent();

    // A credential that cannot be read counts as none, as before: there is
    // nothing to validate.
    const Result<std::optional<QString>> stored = m_secrets.readAccountSession();
    const QString bearer = stored && stored.value() ? *stored.value() : QString();
    if (bearer.isEmpty()) {
        commitSignedOut(token, CredentialChange::Keep, {}, std::nullopt);
        return;
    }
    if (!m_client.isConfigured()) {
        restoreFromHints(token, bearer,
                         Failure::of(FailureKind::NotAvailable, QStringLiteral("account service is not configured")));
        return;
    }

    // The lease is held until this restore's answer arrives, even when a
    // newer intent makes the answer stale.
    m_restoreToken = token;
    m_restoreLease = std::move(lease);
    m_client.fetchProfile(bearer).then(this, [this, token, bearer](const Result<AccountProfile>& profile) {
        onRestoreProfile(token, bearer, profile);
    });
}

void AccountSessionService::onRestoreProfile(Token token, const QString& bearer, const Result<AccountProfile>& profile)
{
    const bool wasRestoring = m_restoreToken == token && m_restoreLease.has_value();
    if (m_restoreToken == token) {
        m_restoreLease.reset();
    }
    applyRestoreProfile(token, bearer, profile);
    if (wasRestoring) {
        emit restoreFinished();
    }
}

void AccountSessionService::applyRestoreProfile(Token token, const QString& bearer,
                                                const Result<AccountProfile>& profile)
{
    if (!m_intents.isCurrent(token)) {
        return;
    }
    if (profile) {
        // The credential was replaced or removed while the check ran.
        if (!commitValidated(token, profile.value(), bearer, false)) {
            commitSignedOut(token, CredentialChange::Keep, {}, std::nullopt);
        }
        return;
    }
    const int status = profile.failure().httpStatus;
    if (status == 401 || status == 403) {
        commitSignedOut(token, CredentialChange::RemoveIfMatches, bearer, std::nullopt);
        return;
    }
    restoreFromHints(token, bearer, profile.failure());
}

void AccountSessionService::restoreFromHints(Token token, const QString& bearer, const Failure& reason)
{
    const AccountProfile hints = profileHints();
    if (hints.id.isEmpty()) {
        commitSignedOut(token, CredentialChange::Keep, {}, std::nullopt);
        return;
    }
    commitOffline(token, hints, bearer, reason);
}

bool AccountSessionService::removeLocalSession()
{
    const Token token = beginIntent();
    return commitSignedOut(token, CredentialChange::RemoveAll, {}, std::nullopt);
}

std::optional<AccountOperationContext> AccountSessionService::captureOperation() const
{
    if ((m_state != AccountSessionState::Validated && m_state != AccountSessionState::Offline)
        || m_profile.id.isEmpty() || m_bearerSession.isEmpty()) {
        return std::nullopt;
    }
    return AccountOperationContext{m_profile.id, m_bearerSession, m_state, m_generation};
}

bool AccountSessionService::isCurrent(const AccountOperationContext& context) const
{
    return context.generation == m_generation && context.ownerId == m_profile.id
        && context.bearerSession == m_bearerSession && context.state == m_state
        && (m_state == AccountSessionState::Validated || m_state == AccountSessionState::Offline);
}

void AccountSessionService::handleUnauthorized(const AccountOperationContext& context)
{
    if (!isCurrent(context)) {
        return;
    }
    const Token token = beginIntent();
    commitSignedOut(token, CredentialChange::RemoveIfMatches, context.bearerSession, std::nullopt);
}

bool AccountSessionService::commitValidated(Token token, const AccountProfile& profile, const QString& bearer,
                                            bool storeCredential)
{
    if (profile.id.isEmpty() || bearer.isEmpty() || !m_intents.isCurrent(token)) {
        return false;
    }
    if (storeCredential) {
        const Result<void> written = m_secrets.writeAccountSession(bearer);
        if (!written || !storedSessionIs(bearer)) {
            if (written) {
                // Best effort: a session that cannot be read back must not
                // be found by the next restore. If removal fails as well,
                // that restore validates it like any other stored session.
                [[maybe_unused]] const Result<platform::CredentialRemoval> removed =
                    m_secrets.removeAccountSessionIfMatches(bearer);
            }
            return false;
        }
    } else if (!storedSessionIs(bearer)) {
        return false;
    }
    publish(AccountSessionState::Validated, profile, bearer, std::nullopt, true);
    return true;
}

bool AccountSessionService::commitOffline(Token token, const AccountProfile& profile, const QString& bearer,
                                          const Failure& reason)
{
    if (profile.id.isEmpty() || bearer.isEmpty() || !m_intents.isCurrent(token) || !storedSessionIs(bearer)) {
        return false;
    }
    publish(AccountSessionState::Offline, profile, bearer, reason, false);
    return true;
}

QFuture<bool> AccountSessionService::logoutForLifecycle(const AccountOperationContext& context)
{
    const Token token = beginIntent();
    if (!isCurrent(context)) {
        return QtFuture::makeReadyValueFuture(false);
    }
    return m_client.logout(context.bearerSession).then(this, [this, token, context](const Result<void>& result) {
        if (!m_intents.isCurrent(token) || !isCurrent(context)) {
            return false;
        }
        if (!result && result.failure().kind != FailureKind::Unauthorized) {
            m_lastFailure = result.failure();
            return false;
        }
        return commitSignedOut(token, CredentialChange::RemoveIfMatches, context.bearerSession, std::nullopt);
    });
}

bool AccountSessionService::commitSignedOut(Token token, CredentialChange change, const QString& expectedSession,
                                            std::optional<Failure> reason)
{
    if (!m_intents.isCurrent(token)) {
        return false;
    }
    switch (change) {
    case CredentialChange::Keep:
        break;
    case CredentialChange::RemoveAll:
        if (!m_secrets.removeAccountSession()) {
            return false;
        }
        break;
    case CredentialChange::RemoveIfMatches: {
        // A different stored session belongs to a newer sign-in: it stays,
        // and so does this state.
        const Result<platform::CredentialRemoval> removed = m_secrets.removeAccountSessionIfMatches(expectedSession);
        if (!removed || removed.value() == platform::CredentialRemoval::Mismatch) {
            return false;
        }
        break;
    }
    }
    publish(AccountSessionState::SignedOut, {}, {}, std::move(reason), false);
    return true;
}

void AccountSessionService::publish(AccountSessionState state, const AccountProfile& profile, const QString& bearer,
                                    std::optional<Failure> reason, bool persistHints)
{
    const QString previousOwner = m_profile.id;
    ++m_generation;
    m_state = state;
    m_profile = profile;
    m_bearerSession = bearer;
    m_lastFailure = std::move(reason);
    if (persistHints && !profile.id.isEmpty()) {
        persistProfileHints(profile);
    }
    emit stateChanged();
    if (previousOwner != m_profile.id) {
        emit ownerChanged(m_profile.id);
    }
}

bool AccountSessionService::storedSessionIs(const QString& bearer)
{
    const Result<std::optional<QString>> stored = m_secrets.readAccountSession();
    return stored && stored.value() && *stored.value() == bearer;
}

AccountProfile AccountSessionService::profileHints() const
{
    AccountProfile hints;
    hints.id = m_settings.string(keys::AccountOwnerHint);
    hints.displayName = m_settings.string(keys::AccountDisplayNameHint);
    hints.username = m_settings.string(keys::AccountUsernameHint);
    hints.planLabel = m_settings.string(keys::AccountPlanHint);
    return hints;
}

void AccountSessionService::persistProfileHints(const AccountProfile& profile)
{
    m_settings.setString(keys::AccountOwnerHint, profile.id);
    m_settings.setString(keys::AccountDisplayNameHint, profile.displayName);
    m_settings.setString(keys::AccountUsernameHint, profile.username);
    m_settings.setString(keys::AccountPlanHint, profile.planLabel);
}

} // namespace lmp::core::account
