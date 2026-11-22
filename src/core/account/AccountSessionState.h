#pragma once

#include <QString>
#include <QtGlobal>

namespace lmp::core::account {

enum class AccountSessionState {
    SignedOut,
    // A browser sign-in is running. The previous profile stays until it ends.
    SigningIn,
    // The service confirmed the stored session.
    Validated,
    // A stored session that could not be checked; the profile comes from
    // the hints saved at the last validation.
    Offline,
};

// What an account request captured when it started, so its result can be
// dropped, or its 401 ignored, once the session it belonged to is gone.
struct AccountOperationContext {
    QString ownerId;
    QString bearerSession;
    AccountSessionState state = AccountSessionState::SignedOut;
    quint64 generation = 0;
};

} // namespace lmp::core::account
