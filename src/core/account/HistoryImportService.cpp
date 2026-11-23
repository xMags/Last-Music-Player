#include "core/account/HistoryImportService.h"

#include "core/account/HistoryImport.h"
#include "core/library/UserDataOperationGate.h"
#include "core/remote/TrackJson.h"

namespace lmp::core::account {

namespace {

constexpr QLatin1StringView Declined{"declined"};
constexpr QLatin1StringView Accepted{"accepted"};
constexpr QLatin1StringView Completed{"completed"};

// A song the service does not know is left on this PC; anything else stops
// the import until the next sync.
[[nodiscard]] bool skippable(const Failure& failure)
{
    return failure.kind == FailureKind::NotFound || failure.httpStatus == 404
        || failure.kind == FailureKind::InvalidResponse;
}

} // namespace

HistoryImportService::HistoryImportService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
}

bool HistoryImportService::stillCurrent() const
{
    return !m_owner.isEmpty() && m_deps.accountId() == m_owner && m_deps.gate.isCurrent(m_gateGeneration);
}

void HistoryImportService::offerIfDue()
{
    if (m_offering || m_importing) {
        return;
    }
    const QString owner = m_deps.accountId();
    if (owner.isEmpty()) {
        return;
    }
    const db::AccountSyncState state = m_deps.loadState(owner);
    if (state.legacyHistoryImportState == Declined || state.legacyHistoryImportState == Completed) {
        return;
    }
    m_owner = owner;
    m_gateGeneration = m_deps.gate.generation();
    if (state.legacyHistoryImportState == Accepted) {
        start();
        return;
    }
    // Nothing to import: no question.
    if (m_deps.candidates().isEmpty()) {
        return;
    }
    m_offering = true;
    emit offerChanged();
}

bool HistoryImportService::saveDecision(const QString& value)
{
    std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease || !stillCurrent()) {
        return false;
    }
    db::AccountSyncState state = m_deps.loadState(m_owner);
    state.accountId = m_owner;
    state.legacyHistoryImportState = value;
    return m_deps.saveState(state);
}

void HistoryImportService::accept()
{
    if (!m_offering) {
        return;
    }
    m_offering = false;
    emit offerChanged();
    if (!saveDecision(Accepted)) {
        return;
    }
    start();
}

void HistoryImportService::decline()
{
    if (!m_offering) {
        return;
    }
    m_offering = false;
    emit offerChanged();
    (void)saveDecision(Declined);
}

void HistoryImportService::start()
{
    if (!stillCurrent()) {
        return;
    }
    m_queue = m_deps.candidates();
    m_next = 0;
    m_imported = 0;
    m_skipped = 0;
    ++m_run;
    m_importing = true;
    emit importingChanged();
    step();
}

void HistoryImportService::step()
{
    while (m_importing) {
        if (!stillCurrent()) {
            stop();
            return;
        }
        if (m_next >= m_queue.size()) {
            complete();
            return;
        }
        const db::HistoryImportCandidate candidate = m_queue.at(m_next++);
        if (!remote::track_json::isCompatibleAccountRemoteTrack(candidate.track, false)) {
            ++m_skipped;
            continue;
        }
        const QString playedAt = normalizeLegacyPlayedAt(candidate.lastPlayedUtc);
        const QString eventId = historyImportEventId(m_owner, candidate.sourceKey);
        if (playedAt.isEmpty() || eventId.isEmpty()) {
            ++m_skipped;
            continue;
        }
        const std::uint64_t run = m_run;
        m_deps.resolve(candidate.track.sourceUrl)
            .then(this, [this, run, candidate, eventId, playedAt](const Result<QByteArray>& answer) {
                handleResolved(run, candidate, eventId, playedAt, answer);
            })
            .onCanceled(this, [this, run]() {
                if (run == m_run) {
                    stop();
                }
            });
        return; // carries on when the answer comes
    }
}

void HistoryImportService::handleResolved(std::uint64_t run, const db::HistoryImportCandidate& candidate,
                                          const QString& eventId, const QString& playedAt,
                                          const Result<QByteArray>& answer)
{
    if (run != m_run || !m_importing) {
        return;
    }
    if (!stillCurrent()) {
        stop();
        return;
    }
    if (!answer) {
        if (answer.failure().kind == FailureKind::Cancelled) {
            stop();
            return;
        }
        if (!skippable(answer.failure())) {
            pause();
            return;
        }
        ++m_skipped;
        step();
        return;
    }
    const std::optional<Track> resolved = resolvedImportTrack(answer.value(), candidate.track);
    if (!resolved) {
        ++m_skipped;
        step();
        return;
    }
    db::PlaybackEventRecord event;
    event.eventId = eventId;
    event.accountId = m_owner;
    event.remoteTrackId = resolved->remoteId;
    event.track = *resolved;
    event.playedAtUtc = playedAt;
    event.positionSeconds = 0.0;
    std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease || !stillCurrent()) {
        stop();
        return;
    }
    const bool queued = m_deps.enqueue(event);
    lease.reset();
    if (!queued) {
        pause();
        return;
    }
    ++m_imported;
    step();
}

void HistoryImportService::complete()
{
    if (!saveDecision(Completed)) {
        pause();
        return;
    }
    const int imported = m_imported;
    const int skipped = m_skipped;
    stop();
    if (imported > 0) {
        m_deps.sendHistory();
    }
    emit finished(imported, skipped);
}

void HistoryImportService::pause()
{
    stop();
    emit paused(tr("History import paused. It will resume on the next synchronization."));
}

void HistoryImportService::stop()
{
    ++m_run;
    m_queue.clear();
    m_next = 0;
    if (m_importing) {
        m_importing = false;
        emit importingChanged();
    }
}

} // namespace lmp::core::account
