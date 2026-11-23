#include "core/account/AccountDataLifecycleService.h"

#include "core/account/AccountDataLifecycleStorage.h"
#include "core/account/AccountSessionService.h"
#include "core/library/UserDataOperationGate.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <algorithm>
#include <utility>

namespace lmp::core::account {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(50);
constexpr qsizetype kMaximumOwnerIdLength = 512;

using Outcome = AccountLogoutOutcome;
using CleanupOutcome = AccountCleanupOutcome;
using Promise = std::shared_ptr<QPromise<AccountLogoutResult>>;
using CleanupPromise = std::shared_ptr<QPromise<AccountCleanupResult>>;

[[nodiscard]] bool isAuthenticated(const AccountSessionService& session)
{
    return (session.state() == AccountSessionState::Validated || session.state() == AccountSessionState::Offline)
        && !session.profile().id.isEmpty();
}

[[nodiscard]] bool isValidOwnerId(const QString& ownerId)
{
    if (ownerId.isEmpty() || ownerId.size() > kMaximumOwnerIdLength) {
        return false;
    }
    return std::none_of(ownerId.cbegin(), ownerId.cend(), [](QChar character) { return character.unicode() < u' '; });
}

template <typename T>
void settle(const std::shared_ptr<QPromise<T>>& promise, T result)
{
    promise->addResult(std::move(result));
    promise->finish();
}

} // namespace

AccountDataLifecycleService::AccountDataLifecycleService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    m_poll.setInterval(kPollInterval);
    connect(&m_poll, &QTimer::timeout, this, &AccountDataLifecycleService::onPoll);
}

void AccountDataLifecycleService::waitForIdle(Step then)
{
    m_afterWait = std::move(then);
    m_waitDeadline = std::chrono::steady_clock::now() + m_deps.idleTimeout;
    // The first look happens on the next turn, never inside the caller.
    m_poll.start();
}

void AccountDataLifecycleService::onPoll()
{
    const bool idle = m_deps.gate.waitForIdle(std::chrono::milliseconds(0));
    if (!idle && std::chrono::steady_clock::now() < *m_waitDeadline) {
        return;
    }
    m_poll.stop();
    m_waitDeadline.reset();
    Step then = std::exchange(m_afterWait, {});
    then(idle);
}

QFuture<AccountLogoutResult> AccountDataLifecycleService::signOut()
{
    auto promise = std::make_shared<QPromise<AccountLogoutResult>>();
    promise->start();
    QFuture<AccountLogoutResult> future = promise->future();
    const auto finish = [this, promise](AccountLogoutResult result) {
        m_signingOut = false;
        emit signOutFinished(result);
        settle(promise, std::move(result));
    };

    if (m_signingOut) {
        settle(promise, AccountLogoutResult{Outcome::CleanupBusy, false,
                                            tr("Another cleanup operation is already in progress.")});
        return future;
    }
    const std::optional<AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context) {
        settle(promise, AccountLogoutResult{Outcome::NoAuthenticatedAccount, false,
                                            tr("No signed-in account is available.")});
        return future;
    }
    if (!m_deps.storage.isAccountDataStoreReady()) {
        settle(promise, AccountLogoutResult{Outcome::CleanupDeferred, false,
                                            tr("Account data is not available yet. Try signing out again shortly.")});
        return future;
    }
    m_signingOut = true;

    m_deps.outboundSync.requestOutboundSync().then(this, [this, finish, context = *context](bool synchronized) {
        if (!m_deps.session.isCurrent(context)) {
            if (m_deps.session.state() == AccountSessionState::SignedOut && m_deps.session.profile().id.isEmpty()) {
                finish({Outcome::FinalSyncFailed, true,
                        tr("The account session expired during final sync. Local account data was preserved.")});
            } else {
                finish({Outcome::SessionChanged, false, tr("The active account changed. Nothing was removed.")});
            }
            return;
        }
        const QString owner = context.ownerId;
        const std::optional<std::size_t> pending = m_deps.storage.recoverablePendingChangeCount(owner);
        if (!pending) {
            finish({Outcome::FinalSyncFailed, false,
                    tr("Pending account changes could not be verified. The account remains signed in.")});
            return;
        }
        if (*pending > 0) {
            finish({Outcome::FinalSyncFailed, false,
                    synchronized ? tr("Some account changes are still waiting to synchronize. The account remains "
                                      "signed in.")
                                 : tr("Pending account changes could not be synchronized. The account remains signed "
                                      "in.")});
            return;
        }
        if (!m_deps.gate.closeAdmissions()) {
            finish({Outcome::CleanupBusy, false, tr("Another cleanup operation is already in progress.")});
            return;
        }
        // From here on every ending reopens the gate.
        const auto finishOpen = [this, finish](AccountLogoutResult result) {
            m_deps.gate.reopen();
            finish(std::move(result));
        };
        waitForIdle([this, finishOpen, context](bool idle) {
            if (!idle) {
                finishOpen({Outcome::CleanupTimedOut, false,
                            tr("A background data operation did not finish. The account remains signed in.")});
                return;
            }
            if (!m_deps.session.isCurrent(context)) {
                finishOpen({Outcome::SessionChanged, false, tr("The active account changed. Nothing was removed.")});
                return;
            }
            // Admissions are closed and every earlier change has landed, so
            // this count is stable: a change that arrived after the sync stops
            // the sign-out instead of being thrown away.
            const QString owner = context.ownerId;
            const std::optional<std::size_t> stable = m_deps.storage.recoverablePendingChangeCount(owner);
            if (!stable || *stable > 0) {
                finishOpen({Outcome::FinalSyncFailed, false,
                            tr("A new account change arrived during sign out. The account remains signed in.")});
                return;
            }
            if (!m_deps.settings.trySetString(settings::keys::PendingAccountCleanupOwnerId, owner)) {
                finishOpen({Outcome::MarkerWriteFailed, false,
                            tr("Could not prepare safe account cleanup. The account remains signed in.")});
                return;
            }
            m_deps.session.logoutForLifecycle(context).then(this, [this, finishOpen, owner](bool signedOut) {
                if (!signedOut) {
                    m_deps.settings.remove(settings::keys::PendingAccountCleanupOwnerId);
                    finishOpen({Outcome::RemoteLogoutFailed, false,
                                tr("Could not complete sign out. The account and its local data were preserved.")});
                    return;
                }
                if (!m_deps.storage.removeSignedOutAccountData(owner)) {
                    finishOpen({Outcome::CleanupDeferred, true,
                                tr("Signed out. Local account cleanup will retry automatically next time the app "
                                   "starts.")});
                    return;
                }
                emit accountDataRemoved(owner);
                if (!m_deps.settings.remove(settings::keys::PendingAccountCleanupOwnerId)) {
                    finishOpen({Outcome::CleanupDeferred, true,
                                tr("Signed out. Cleanup was completed, but its recovery marker will be checked again "
                                   "at startup.")});
                    return;
                }
                finishOpen({Outcome::Succeeded, true, {}});
            });
        });
    });
    return future;
}

QFuture<AccountCleanupResult> AccountDataLifecycleService::resolvePendingCleanup()
{
    auto promise = std::make_shared<QPromise<AccountCleanupResult>>();
    promise->start();
    QFuture<AccountCleanupResult> future = promise->future();
    const QString marker = m_deps.settings.string(settings::keys::PendingAccountCleanupOwnerId);
    if (marker.isEmpty()) {
        settle(promise, AccountCleanupResult{CleanupOutcome::NothingPending, {}});
        return future;
    }
    if (!isValidOwnerId(marker)) {
        const bool removed = m_deps.settings.remove(settings::keys::PendingAccountCleanupOwnerId);
        settle(promise, removed ? AccountCleanupResult{CleanupOutcome::InvalidMarkerRemoved, {}}
                                : AccountCleanupResult{CleanupOutcome::MarkerWriteFailed,
                                                       tr("A pending account cleanup marker could not be repaired.")});
        return future;
    }
    const auto preserve = [this](const CleanupPromise& target) {
        const bool removed = m_deps.settings.remove(settings::keys::PendingAccountCleanupOwnerId);
        settle(target, removed ? AccountCleanupResult{CleanupOutcome::PreservedAuthenticatedAccount, {}}
                               : AccountCleanupResult{CleanupOutcome::MarkerWriteFailed,
                                                      tr("The signed-in account was preserved, but cleanup recovery "
                                                         "could not be finalized.")});
    };
    // The same account signed in again: its data is its own again.
    if (isAuthenticated(m_deps.session) && m_deps.session.profile().id == marker) {
        preserve(promise);
        return future;
    }
    if (!m_deps.storage.isAccountDataStoreReady()) {
        settle(promise, AccountCleanupResult{CleanupOutcome::CleanupDeferred,
                                             tr("Pending account cleanup will retry when local data is available.")});
        return future;
    }
    if (!m_deps.gate.closeAdmissions()) {
        settle(promise, AccountCleanupResult{CleanupOutcome::CleanupBusy,
                                             tr("Pending account cleanup is already in progress.")});
        return future;
    }
    waitForIdle([this, promise, marker, preserve](bool idle) {
        const auto end = [this, promise](AccountCleanupResult result) {
            m_deps.gate.reopen();
            settle(promise, std::move(result));
        };
        if (!idle) {
            end({CleanupOutcome::CleanupTimedOut,
                 tr("Pending account cleanup is waiting for a background data operation.")});
            return;
        }
        if (isAuthenticated(m_deps.session) && m_deps.session.profile().id == marker) {
            m_deps.gate.reopen();
            preserve(promise);
            return;
        }
        if (!m_deps.storage.removeSignedOutAccountData(marker)) {
            end({CleanupOutcome::CleanupDeferred,
                 tr("Pending account cleanup will retry automatically next time the app starts.")});
            return;
        }
        emit accountDataRemoved(marker);
        if (!m_deps.settings.remove(settings::keys::PendingAccountCleanupOwnerId)) {
            end({CleanupOutcome::MarkerWriteFailed,
                 tr("Account data was cleaned, but its recovery marker could not be removed.")});
            return;
        }
        end({CleanupOutcome::Cleaned, {}});
    });
    return future;
}

} // namespace lmp::core::account
