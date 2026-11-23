#include "core/account/AccountSignOutCoordinator.h"

#include "core/account/AccountSessionService.h"

namespace lmp::core::account {

LocalAccountSignOut::LocalAccountSignOut(AccountSessionService& session)
    : m_session(session)
{
}

SignOutOutcome LocalAccountSignOut::signOut()
{
    return m_session.removeLocalSession() ? SignOutOutcome::SignedOut : SignOutOutcome::CredentialRemovalFailed;
}

} // namespace lmp::core::account
