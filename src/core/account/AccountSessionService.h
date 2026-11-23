#pragma once

#include "core/account/AccountSessionProtocol.h"
#include "core/account/AccountSessionState.h"
#include "core/account/LoopbackCallback.h"
#include "core/async/RequestScope.h"
#include "core/async/Result.h"
#include "core/library/UserDataOperationGate.h"

#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace lmp::core {
class SettingsStore;
}

namespace lmp::platform {
class IExternalLinks;
}

namespace lmp::core::account {

class AccountClient;
class SecretStore;

// The signed-in account: browser sign-in, restoring the stored session at
// start, and dropping a session the service no longer accepts.
//
// Every sign-in, restore, local removal and accepted 401 starts a new
// authorization intent; work begun under an older intent ends without
// touching the state or the stored credential. A newer sign-in therefore
// supersedes an older one (whose future finishes as Cancelled), and a restore
// cannot bring back a session that was removed while it waited. Each
// published change bumps generation(), which every captured context checks.
//
// Threading: lives on the GUI thread and is used only there; every
// continuation runs there with this object as its context. Credential reads
// and writes are short synchronous system calls made on that thread. Nothing
// secret (session, code, verifier) or personal (profile id) is ever logged.
class AccountSessionService final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        AccountClient& client;
        SecretStore& secrets;
        SettingsStore& settings;
        platform::IExternalLinks& links;
        // Sign-in and restore are refused while user data is being cleaned up.
        UserDataOperationGate& operationGate;
    };

    // Every dependency must outlive this object.
    explicit AccountSessionService(const Dependencies& dependencies, QObject* parent = nullptr);

    // Whether this build can sign in at all.
    [[nodiscard]] bool isAvailable() const;
    [[nodiscard]] AccountSessionState state() const { return m_state; }
    [[nodiscard]] const AccountProfile& profile() const { return m_profile; }
    [[nodiscard]] quint64 generation() const { return m_generation; }
    // Why the last sign-in or restore did not validate, for the error text
    // table (net::userMessageForSignInFailure); nothing after a success.
    [[nodiscard]] const std::optional<Failure>& lastFailure() const { return m_lastFailure; }

    // Restores the stored session: SignedOut without one; Offline from the
    // saved hints when the build has no service or the service could not
    // confirm it (SignedOut without hints); Validated when it was confirmed;
    // SignedOut, removing the credential, when the service answered 401 or
    // 403.
    void restore();

    // Opens the browser sign-in and waits for it: listener, browser, code
    // exchange, profile, then the session is stored and read back before it
    // counts. Finishes with success when the session is validated.
    [[nodiscard]] QFuture<Result<void>> beginSignIn();
    // Stops a sign-in that is still waiting for the browser.
    void cancelSignIn();

    // Forgets the session here, removing its credential. False (and nothing
    // changes) when the credential could not be removed.
    bool removeLocalSession();

    // Ends `context`'s session on the service (POST /v1/auth/logout), then
    // here: true once it is signed out, which a 401 also counts as. False
    // when the service could not be reached or refused, or when the session
    // is no longer `context`'s (a newer sign-in is never ended).
    [[nodiscard]] QFuture<bool> logoutForLifecycle(const AccountOperationContext& context);

    // The session an account request should use, when there is one.
    [[nodiscard]] std::optional<AccountOperationContext> captureOperation() const;
    [[nodiscard]] bool isCurrent(const AccountOperationContext& context) const;
    // The service rejected the session `context` used. Signs out only when
    // that session is still the current one.
    void handleUnauthorized(const AccountOperationContext& context);

signals:
    void stateChanged();
    // The signed-in owner changed (empty when signed out).
    void ownerChanged(const QString& ownerId);

private:
    using Token = RequestScope::Token;
    enum class CredentialChange { Keep, RemoveAll, RemoveIfMatches };

    struct SignInAttempt {
        Token token = 0;
        QPromise<Result<void>> promise;
        std::optional<UserDataOperationGate::Lease> lease;
        QString verifier;
        QString redirectUri;
    };

    Token beginIntent();
    [[nodiscard]] bool isSignInCurrent(Token token) const;
    void openBrowser(Token token, const PkceTransaction& transaction, QFuture<Result<QString>> code);
    void onCallbackFinished(Token token, const Result<QString>& code);
    void onCodeExchanged(Token token, const Result<QString>& bearer);
    void onSignInProfile(Token token, const QString& bearer, const Result<AccountProfile>& profile);
    void failSignIn(Token token, const Failure& failure);
    void finishSignIn(Token token, Result<void> result);
    void onRestoreProfile(Token token, const QString& bearer, const Result<AccountProfile>& profile);
    void restoreFromHints(Token token, const QString& bearer, const Failure& reason);

    bool commitValidated(Token token, const AccountProfile& profile, const QString& bearer, bool storeCredential);
    bool commitOffline(Token token, const AccountProfile& profile, const QString& bearer, const Failure& reason);
    bool commitSignedOut(Token token, CredentialChange change, const QString& expectedSession,
                         std::optional<Failure> reason);
    void publish(AccountSessionState state, const AccountProfile& profile, const QString& bearer,
                 std::optional<Failure> reason, bool persistHints);
    [[nodiscard]] bool storedSessionIs(const QString& bearer);
    [[nodiscard]] AccountProfile profileHints() const;
    void persistProfileHints(const AccountProfile& profile);

    AccountClient& m_client;
    SecretStore& m_secrets;
    SettingsStore& m_settings;
    platform::IExternalLinks& m_links;
    UserDataOperationGate& m_operationGate;

    LoopbackCallback m_callback;
    RequestScope m_intents;
    std::optional<SignInAttempt> m_signIn;
    Token m_restoreToken = 0;
    std::optional<UserDataOperationGate::Lease> m_restoreLease;

    AccountSessionState m_state = AccountSessionState::SignedOut;
    AccountProfile m_profile;
    QString m_bearerSession;
    std::optional<Failure> m_lastFailure;
    quint64 m_generation = 1;
};

} // namespace lmp::core::account
