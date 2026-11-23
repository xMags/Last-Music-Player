#pragma once

#include "core/account/AccountSessionState.h"
#include "core/account/AccountSignOutCoordinator.h"

#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QTimer>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

namespace lmp::core {
class SettingsStore;
class UserDataOperationGate;
} // namespace lmp::core

namespace lmp::core::account {

class AccountDataLifecycleStorage;
class AccountSessionService;

// Sends what is queued here before a sign-out (MusicSyncService does).
class AccountOutboundSync {
public:
    virtual ~AccountOutboundSync() = default;
    // True when everything queued could be sent.
    [[nodiscard]] virtual QFuture<bool> requestOutboundSync() = 0;

protected:
    AccountOutboundSync() = default;
    AccountOutboundSync(const AccountOutboundSync&) = default;
    AccountOutboundSync& operator=(const AccountOutboundSync&) = default;
};

enum class AccountCleanupOutcome {
    NothingPending,
    PreservedAuthenticatedAccount,
    Cleaned,
    InvalidMarkerRemoved,
    CleanupBusy,
    CleanupTimedOut,
    MarkerWriteFailed,
    CleanupDeferred,
    UnexpectedFailure,
};

struct AccountCleanupResult {
    AccountCleanupOutcome outcome = AccountCleanupOutcome::UnexpectedFailure;
    QString safeError;
};

// The whole sign-out, as earlier versions did it, and its recovery at the
// next launch.
//
// Sign-out: send what is queued; stop (the account stays signed in, nothing
// is removed) when changes remain that could still be sent or the session
// changed meanwhile; close the user-data gate and wait for running work (30
// s at most); count again, since a change may have arrived; write the
// PendingAccountCleanupOwnerId marker; end the session on the service; then
// remove the account's data here and the marker. If the removal fails the
// marker stays and the next launch finishes the job (resolvePendingCleanup),
// unless that account is signed in again by then.
//
// Threading: GUI thread only; futures finish there.
class AccountDataLifecycleService final : public QObject, public AccountSignOutCoordinator {
    Q_OBJECT

public:
    struct Dependencies {
        AccountSessionService& session;
        AccountOutboundSync& outboundSync;
        AccountDataLifecycleStorage& storage;
        SettingsStore& settings;
        UserDataOperationGate& gate;
        // How long running work may take to finish; 30 s when not set.
        std::chrono::milliseconds idleTimeout{std::chrono::seconds(30)};
    };

    // Every dependency must outlive this object.
    explicit AccountDataLifecycleService(Dependencies dependencies, QObject* parent = nullptr);

    [[nodiscard]] QFuture<AccountLogoutResult> signOut() override;
    [[nodiscard]] QFuture<AccountCleanupResult> resolvePendingCleanup();

signals:
    // The account's data on this PC was removed.
    void accountDataRemoved(const QString& ownerId);
    // A sign-out ended (successful or not).
    void signOutFinished(const lmp::core::account::AccountLogoutResult& result);

private:
    using Step = std::function<void(bool idle)>;

    void waitForIdle(Step then);
    void onPoll();

    Dependencies m_deps;
    QTimer m_poll;
    std::optional<std::chrono::steady_clock::time_point> m_waitDeadline;
    Step m_afterWait;
    bool m_signingOut = false;
};

} // namespace lmp::core::account
