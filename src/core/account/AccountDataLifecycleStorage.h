#pragma once

#include "core/async/Result.h"

#include <QString>

#include <cstddef>
#include <optional>

namespace lmp::core::account {

// What signing out needs from local storage: whether it can be used, how
// many changes for an account could still be sent, and removing the data of
// an account that has signed out. Other accounts' data, songs on this PC,
// API key server data and settings are never touched.
class AccountDataLifecycleStorage {
public:
    virtual ~AccountDataLifecycleStorage() = default;

    [[nodiscard]] virtual bool isAccountDataStoreReady() const noexcept = 0;
    // Queued changes that are not blocked, or nothing when they cannot be
    // counted (no account, or the store refused).
    [[nodiscard]] virtual std::optional<std::size_t> recoverablePendingChangeCount(const QString& accountId) const = 0;
    [[nodiscard]] virtual Result<void> removeSignedOutAccountData(const QString& accountId) = 0;

protected:
    AccountDataLifecycleStorage() = default;
    AccountDataLifecycleStorage(const AccountDataLifecycleStorage&) = default;
    AccountDataLifecycleStorage& operator=(const AccountDataLifecycleStorage&) = default;
};

} // namespace lmp::core::account
