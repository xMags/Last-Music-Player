// MusicSyncService: requests, the run loop, scheduling and bookkeeping. The
// lanes themselves are in MusicSyncLanes.cpp.

#include "core/account/MusicSyncService.h"
#include "core/app/PerfLog.h"

#include "core/account/AccountSessionService.h"
#include "core/account/AutoSyncPolicy.h"
#include "core/account/PortablePreferenceSettings.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRepository.h"
#include "core/mode/ModeService.h"
#include "core/playback/PlaybackClock.h"
#include "core/settings/SettingsStore.h"

#include <QDateTime>
#include <QFuture>

#include <utility>

namespace lmp::core::account {

namespace {

constexpr std::array<MusicSyncService::Lane, 5> kLaneOrder{
    MusicSyncService::Lane::History, MusicSyncService::Lane::Likes, MusicSyncService::Lane::PreferencePush,
    MusicSyncService::Lane::PreferencePull, MusicSyncService::Lane::Library};

[[nodiscard]] std::uint32_t bits(MusicSyncService::Lane lane)
{
    return static_cast<std::uint32_t>(lane);
}

[[nodiscard]] std::size_t laneIndex(MusicSyncService::Lane lane)
{
    for (std::size_t index = 0; index < kLaneOrder.size(); ++index) {
        if (kLaneOrder[index] == lane) {
            return index;
        }
    }
    return 0;
}

[[nodiscard]] QString systemNowUtc()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

} // namespace

MusicSyncService::MusicSyncService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    if (!m_deps.nowUtc) {
        m_deps.nowUtc = systemNowUtc;
    }
    m_mutationTimer = m_deps.clock.createTimer([this]() {
        const std::uint32_t lanes = std::exchange(m_debouncedLanes, 0U);
        if (lanes != 0) {
            (void)requestSync(static_cast<Lane>(lanes));
        }
    });
    for (std::size_t index = 0; index < LaneCount; ++index) {
        const Lane lane = kLaneOrder[index];
        m_retryTimers[index] = m_deps.clock.createTimer([this, index, lane]() {
            m_retryDue[index] = 0;
            (void)requestSync(lane);
        });
    }
    connect(&m_deps.settings, &SettingsStore::changed, this, &MusicSyncService::onSettingChanged);
}

MusicSyncService::~MusicSyncService()
{
    for (const std::shared_ptr<QPromise<bool>>& waiter : m_waiters) {
        waiter->addResult(false);
        waiter->finish();
    }
}

bool MusicSyncService::isCurrent() const
{
    return m_context && m_modeScope && m_deps.session.isCurrent(*m_context) && m_deps.mode.isCurrent(*m_modeScope);
}

QFuture<bool> MusicSyncService::sync()
{
    for (std::size_t index = 0; index < LaneCount; ++index) {
        m_retryTimers[index]->stop();
        m_retryDue[index] = 0;
        m_laneFailures[index] = 0;
    }
    if (const std::optional<AccountOperationContext> context = m_deps.session.captureOperation()) {
        (void)m_deps.outbox.resetAccountRetrySchedule(context->ownerId);
    }
    return requestSync(Lane::All);
}

QFuture<bool> MusicSyncService::requestOutboundSync()
{
    const Lane outbound = Lane::History | Lane::Likes | Lane::PreferencePush;
    m_debouncedLanes &= ~bits(outbound);
    if (m_debouncedLanes == 0) {
        m_mutationTimer->stop();
    }
    for (const Lane lane : {Lane::History, Lane::Likes, Lane::PreferencePush}) {
        resetLaneFailure(lane);
    }
    const std::optional<AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context || !m_deps.outbox.resetRecoverableAccountRetrySchedule(context->ownerId)) {
        return QtFuture::makeReadyValueFuture(false);
    }
    return requestSync(outbound);
}

QFuture<bool> MusicSyncService::requestSync(Lane lanes)
{
    m_pendingLanes |= bits(lanes);
    auto waiter = std::make_shared<QPromise<bool>>();
    waiter->start();
    QFuture<bool> future = waiter->future();
    m_waiters.push_back(std::move(waiter));
    if (m_syncing) {
        return future; // the running sync takes the lanes on
    }

    m_syncing = true;
    qCDebug(lcPerf) << "sync started";
    emit syncingChanged();
    m_lease = m_deps.gate.tryEnter();
    if (!m_lease) {
        finish(false);
        return future;
    }
    m_allSucceeded = true;
    runNextBatch();
    return future;
}

void MusicSyncService::runNextBatch()
{
    const std::uint32_t lanes = std::exchange(m_pendingLanes, 0U);
    if (lanes == 0) {
        if (m_allSucceeded) {
            setSafeError({});
        }
        finish(m_allSucceeded);
        return;
    }
    if (!m_deps.mode.accountSelected()) {
        finish(false);
        return;
    }
    m_modeScope = m_deps.mode.captureScope();
    m_context = m_deps.session.captureOperation();
    if (!m_context || m_context->state != AccountSessionState::Validated) {
        const bool offline = m_context && m_context->state == AccountSessionState::Offline;
        setSafeError(offline ? tr("Offline. Pending account changes are saved.")
                             : tr("Sign in to synchronize the account library."));
        saveSyncError(offline ? QStringLiteral("offline") : QStringLiteral("signed-out"));
        finish(false);
        return;
    }
    m_batchLanes = lanes;
    runLaneFrom(0);
}

void MusicSyncService::runLaneFrom(std::size_t index)
{
    while (index < kLaneOrder.size() && (m_batchLanes & bits(kLaneOrder[index])) == 0) {
        ++index;
    }
    if (index >= kLaneOrder.size()) {
        runNextBatch(); // lanes asked for meanwhile run next
        return;
    }
    runLane(kLaneOrder[index]).then(this, [this, index](Outcome outcome) {
        switch (outcome) {
        case Outcome::Succeeded:
            break;
        case Outcome::Failed:
            m_allSucceeded = false;
            break;
        case Outcome::Unauthorized:
            if (m_context) {
                m_deps.session.handleUnauthorized(*m_context);
            }
            setSafeError(tr("Your account session has expired. Sign in again to resume synchronization."));
            saveSyncError(QStringLiteral("unauthorized"));
            finish(false);
            return;
        case Outcome::Cancelled:
            finish(false);
            return;
        }
        runLaneFrom(index + 1);
    });
}

void MusicSyncService::finish(bool succeeded)
{
    m_lease.reset();
    m_context.reset();
    m_modeScope.reset();
    m_pendingLanes = 0;
    m_batchLanes = 0;
    std::vector<std::shared_ptr<QPromise<bool>>> waiters = std::exchange(m_waiters, {});
    m_syncing = false;
    qCDebug(lcPerf) << "sync ended";
    emit syncingChanged();
    for (const std::shared_ptr<QPromise<bool>>& waiter : waiters) {
        waiter->addResult(succeeded);
        waiter->finish();
    }
}

QFuture<MusicSyncService::Outcome> MusicSyncService::runLane(Lane lane)
{
    switch (lane) {
    case Lane::History:
        return historyStep(false, 0);
    case Lane::Likes:
        return likesStep(false, 0);
    case Lane::PreferencePush:
        return pushPreferences();
    case Lane::PreferencePull:
        return pullPreferences();
    case Lane::Library:
        return pullLibrary();
    case Lane::None:
    case Lane::All:
        break;
    }
    return QtFuture::makeReadyValueFuture(Outcome::Succeeded);
}

void MusicSyncService::scheduleMutation(Lane lane)
{
    m_debouncedLanes |= bits(lane);
    if (!m_mutationTimer->isActive()) {
        m_mutationTimer->start(MutationDelay);
    }
}

bool MusicSyncService::savePortablePreference(PortablePreferenceKey key, const QString& valueJson)
{
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return false;
    }
    const std::optional<QString> validated = validatePortablePreferenceValue(key, valueJson);
    if (!validated) {
        return false;
    }
    const std::optional<AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context
        || (context->state != AccountSessionState::Validated && context->state != AccountSessionState::Offline)) {
        return false;
    }
    const Result<qint64> revision =
        m_deps.outbox.savePendingAccountPreference(context->ownerId, portablePreferenceName(key), *validated);
    if (!revision || revision.value() <= 0 || !m_deps.session.isCurrent(*context)) {
        return false;
    }
    scheduleMutation(Lane::PreferencePush);
    return true;
}

void MusicSyncService::onSettingChanged(const QString& key)
{
    // Only a change the user made here travels; values that just came from
    // the service do not go back up.
    if (m_applyingRemote || !m_deps.mode.accountSelected()) {
        return;
    }
    const std::optional<PortablePreferenceKey> preference = preferenceForSettingKey(key);
    if (!preference) {
        return;
    }
    (void)savePortablePreference(*preference, localPreferenceValue(m_deps.settings, *preference));
}

db::AccountPendingCounts MusicSyncService::pendingCounts() const
{
    const std::optional<AccountOperationContext> context = m_deps.session.captureOperation();
    return context ? m_deps.outbox.loadAccountPendingCounts(context->ownerId) : db::AccountPendingCounts{};
}

void MusicSyncService::setSafeError(const QString& message)
{
    if (m_lastSafeError == message) {
        return;
    }
    m_lastSafeError = message;
    emit lastSafeErrorChanged();
}

void MusicSyncService::saveSyncError(const QString& code)
{
    if (!isCurrent()) {
        return;
    }
    db::AccountSyncState state = m_deps.accounts.loadAccountSyncState(m_context->ownerId);
    state.accountId = m_context->ownerId;
    state.lastSafeErrorCode = code;
    (void)m_deps.accounts.saveAccountSyncState(state);
}

void MusicSyncService::scheduleRetry(Lane lane, std::chrono::seconds delay)
{
    const std::size_t index = laneIndex(lane);
    const std::uint64_t due =
        m_deps.clock.nowMilliseconds() + static_cast<std::uint64_t>(std::chrono::milliseconds(delay).count());
    // An earlier retry already waiting is kept.
    if (m_retryTimers[index]->isActive() && due >= m_retryDue[index]) {
        return;
    }
    m_retryDue[index] = due;
    m_retryTimers[index]->start(delay);
}

void MusicSyncService::scheduleNextDurableRetry(Lane lane, db::AccountRetryQueue queue)
{
    if (!isCurrent()) {
        return;
    }
    if (const std::optional<int> delay = m_deps.outbox.secondsUntilNextAccountRetry(m_context->ownerId, queue)) {
        scheduleRetry(lane, std::chrono::seconds(*delay));
    }
}

int MusicSyncService::recordLaneFailure(Lane lane)
{
    return ++m_laneFailures[laneIndex(lane)];
}

void MusicSyncService::resetLaneFailure(Lane lane)
{
    const std::size_t index = laneIndex(lane);
    m_laneFailures[index] = 0;
    m_retryTimers[index]->stop();
    m_retryDue[index] = 0;
}

} // namespace lmp::core::account
