#pragma once

#include <QFuture>
#include <QString>

namespace lmp::core::account {

class AccountSessionService;

// How a sign-out ended (the earlier app's outcomes).
enum class AccountLogoutOutcome {
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

struct AccountLogoutResult {
    AccountLogoutOutcome outcome = AccountLogoutOutcome::UnexpectedFailure;
    // The account is signed out now (even when its clean-up was deferred).
    bool signedOut = false;
    // What the user is told, or empty after a clean sign-out.
    QString safeError;
};

// Signing out of the account. AccountDataLifecycleService does the whole
// sign-out (a last sync of pending changes, the service's logout, clearing
// the account's data here); LocalAccountSignOut only forgets the session on
// this PC. Either way the mode service sees the session end and falls back
// to LocalOnly.
//
// GUI thread only; the future finishes there.
class AccountSignOutCoordinator {
public:
    virtual ~AccountSignOutCoordinator() = default;

    [[nodiscard]] virtual QFuture<AccountLogoutResult> signOut() = 0;

protected:
    AccountSignOutCoordinator() = default;
    AccountSignOutCoordinator(const AccountSignOutCoordinator&) = default;
    AccountSignOutCoordinator& operator=(const AccountSignOutCoordinator&) = default;
};

// Removes the session's credential and signs out here only.
class LocalAccountSignOut final : public AccountSignOutCoordinator {
public:
    // The session service is owned by the composition root and outlives this.
    explicit LocalAccountSignOut(AccountSessionService& session);

    [[nodiscard]] QFuture<AccountLogoutResult> signOut() override;

private:
    AccountSessionService& m_session;
};

} // namespace lmp::core::account
