#pragma once

namespace lmp::core::account {

class AccountSessionService;

enum class SignOutOutcome {
    SignedOut,
    // The stored session could not be removed; nothing changed.
    CredentialRemovalFailed,
};

// Signing out of the account. The full sign-out (a last sync of pending
// changes, the service's logout call, clearing the account's local data)
// arrives with account sync; until then LocalAccountSignOut forgets the
// session on this PC. Either way the mode service sees the session end and
// falls back to LocalOnly.
//
// GUI thread only.
class AccountSignOutCoordinator {
public:
    virtual ~AccountSignOutCoordinator() = default;

    [[nodiscard]] virtual SignOutOutcome signOut() = 0;

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

    [[nodiscard]] SignOutOutcome signOut() override;

private:
    AccountSessionService& m_session;
};

} // namespace lmp::core::account
