#include "core/account/AccountSyncScheduler.h"

#include "core/account/AccountSessionService.h"
#include "core/account/AutoSyncPolicy.h"
#include "core/account/MusicSyncService.h"
#include "core/mode/ModeService.h"
#include "core/playback/PlaybackClock.h"
#include "platform/INetworkStatus.h"

#include <QMetaObject>

#include <chrono>

namespace lmp::core::account {

AccountSyncScheduler::AccountSyncScheduler(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(dependencies)
{
    // Launch counts as an attempt: the restore at start syncs on its own, and
    // the window's first activation must not run a second one beside it.
    m_lastAttemptMs = m_deps.clock.nowMilliseconds();
    m_poll = m_deps.clock.createTimer([this]() {
        m_poll->start(AutoSyncPollInterval);
        recoverConnection();
    });
    m_poll->start(AutoSyncPollInterval);
    m_offlineRecheck = m_deps.clock.createTimer([this]() {
        if (m_deps.mode.accountSelected()
            && m_deps.session.state() == AccountSessionState::Offline) {
            m_deps.session.restore();
            m_offlineRecheck->start(OfflineRecheckInterval);
        }
    });

    connect(&m_deps.session, &AccountSessionService::stateChanged, this, &AccountSyncScheduler::onSessionOrModeChanged);
    connect(&m_deps.mode, &mode::ModeService::changed, this, &AccountSyncScheduler::onSessionOrModeChanged);
    m_deps.network.setChangeHandler([this](bool online, bool) {
        if (online) {
            recoverConnection();
        }
    });
}

AccountSyncScheduler::~AccountSyncScheduler()
{
    m_deps.network.setChangeHandler({});
}

void AccountSyncScheduler::onSessionOrModeChanged()
{
    const bool offline = m_deps.mode.accountSelected()
        && m_deps.session.state() == AccountSessionState::Offline;
    if (offline && !m_offlineRecheck->isActive()) {
        m_offlineRecheck->start(OfflineRecheckInterval);
    } else if (!offline) {
        m_offlineRecheck->stop();
    }

    const bool ready = m_deps.mode.accountSelected()
        && m_deps.session.state() == AccountSessionState::Validated;
    const bool becameReady = ready && !m_wasReady;
    m_wasReady = ready;
    if (!becameReady) {
        return;
    }
    m_lastAttemptMs = m_deps.clock.nowMilliseconds();
    // Started on the next turn: the session announces a new owner just after
    // its new state, and the mode service and the database follow that owner
    // before a sync may start.
    QMetaObject::invokeMethod(
        this,
        [this]() {
            if (m_deps.mode.accountSelected()
                && m_deps.session.state() == AccountSessionState::Validated) {
                (void)m_deps.sync.sync();
            }
        },
        Qt::QueuedConnection);
}

void AccountSyncScheduler::windowActivated()
{
    requestBackgroundSync(false);
}

void AccountSyncScheduler::recoverConnection()
{
    if (!m_deps.mode.accountSelected()) {
        return;
    }
    if (m_deps.session.state() == AccountSessionState::Offline) {
        // Validating again starts the sync through onSessionOrModeChanged.
        m_deps.session.restore();
        return;
    }
    requestBackgroundSync(true);
}

void AccountSyncScheduler::requestBackgroundSync(bool fromTimer)
{
    AutoSyncConditions conditions;
    conditions.accountMode = m_deps.mode.accountSelected();
    conditions.sessionValidated = m_deps.session.state() == AccountSessionState::Validated;
    const std::uint64_t now = m_deps.clock.nowMilliseconds();
    if (m_lastAttemptMs) {
        conditions.sinceLastAttempt = std::chrono::milliseconds(static_cast<qint64>(now - *m_lastAttemptMs));
    }
    if (evaluateAutoSync(conditions, fromTimer ? AutoSyncTrigger::Timer : AutoSyncTrigger::WindowFocus)
        != AutoSyncDecision::Run) {
        return;
    }
    // Stamped before the sync, so a slow one cannot let activations pile up.
    m_lastAttemptMs = now;
    (void)m_deps.sync.requestSync(MusicSyncService::Lane::All);
}

} // namespace lmp::core::account
