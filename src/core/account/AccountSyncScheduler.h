#pragma once

#include "core/account/AccountSessionState.h"

#include <QObject>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

namespace lmp::platform {
class INetworkStatus;
}

namespace lmp::core {
class PlaybackClock;
class PlaybackTimer;
namespace mode {
class ModeService;
}
} // namespace lmp::core

namespace lmp::core::account {

class AccountSessionService;
class MusicSyncService;

// How often an Offline session in Account mode is checked again with the
// service (the reference client's interval for an offline start).
inline constexpr std::chrono::seconds OfflineRecheckInterval{15};

// When library sync runs on its own, as earlier versions decided it: a full
// sync when the session becomes Validated in Account mode (sign-in, restore)
// or Account mode is entered with a validated session; a background sync
// when the window is activated (at most once per AutoSyncFocusDebounce),
// every AutoSyncPollInterval, and when the network comes back. On the timer
// and on network return an Offline session is restored first; it syncs once
// it validates. While a session is Offline in Account mode it is also checked
// again every OfflineRecheckInterval. Launch counts as a background attempt.
//
// Threading: GUI thread only.
class AccountSyncScheduler final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        MusicSyncService& sync;
        AccountSessionService& session;
        mode::ModeService& mode;
        platform::INetworkStatus& network;
        PlaybackClock& clock;
    };

    // Every dependency must outlive this object. Starts the poll timer.
    explicit AccountSyncScheduler(const Dependencies& dependencies, QObject* parent = nullptr);
    ~AccountSyncScheduler() override;

    AccountSyncScheduler(const AccountSyncScheduler&) = delete;
    AccountSyncScheduler& operator=(const AccountSyncScheduler&) = delete;

    // The main window became the active window.
    void windowActivated();
    // The periodic check and the network coming back.
    void recoverConnection();

private:
    void onSessionOrModeChanged();
    void requestBackgroundSync(bool fromTimer);

    Dependencies m_deps;
    std::unique_ptr<PlaybackTimer> m_poll;
    std::unique_ptr<PlaybackTimer> m_offlineRecheck;
    std::optional<std::uint64_t> m_lastAttemptMs;
    // Whether the last look saw a validated session in Account mode.
    bool m_wasReady = false;
};

} // namespace lmp::core::account
