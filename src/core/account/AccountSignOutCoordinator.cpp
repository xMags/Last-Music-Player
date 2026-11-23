#include "core/account/AccountSignOutCoordinator.h"

#include "core/account/AccountSessionService.h"

namespace lmp::core::account {

LocalAccountSignOut::LocalAccountSignOut(AccountSessionService& session)
    : m_session(session)
{
}

QFuture<AccountLogoutResult> LocalAccountSignOut::signOut()
{
    if (m_session.removeLocalSession()) {
        return QtFuture::makeReadyValueFuture(AccountLogoutResult{AccountLogoutOutcome::Succeeded, true, {}});
    }
    return QtFuture::makeReadyValueFuture(AccountLogoutResult{
        AccountLogoutOutcome::UnexpectedFailure, false, QStringLiteral("Could not safely complete account sign out.")});
}

} // namespace lmp::core::account
