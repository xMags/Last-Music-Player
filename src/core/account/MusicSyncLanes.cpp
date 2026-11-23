// MusicSyncService's lanes: history, likes, preference push and pull, and
// the library. Each returns how it went; the run loop decides what follows.

#include "core/account/AccountLibrarySync.h"
#include "core/account/AccountSessionService.h"
#include "core/account/AccountSyncTransport.h"
#include "core/account/AutoSyncPolicy.h"
#include "core/account/MusicSyncService.h"
#include "core/account/PortablePreferenceSettings.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRepository.h"
#include "core/settings/SettingsStore.h"

#include <QFuture>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace lmp::core::account {

namespace {

// A safety stop for a queue whose rows could not be marked: the earlier
// app looped until the queue was empty, which a failing write never makes it.
constexpr int kMaxRounds = 1000;
constexpr int kBatchSize = 100;
// Every queued row, for laying unsent changes over the library.
constexpr int kAllRows = 10000;

template <typename T>
[[nodiscard]] QFuture<T> ready(T value)
{
    return QtFuture::makeReadyValueFuture(std::move(value));
}

// The service was busy or unreachable; the same request may work later.
[[nodiscard]] bool isTransientStatus(int status)
{
    return status == 408 || status == 429 || status >= 500;
}

[[nodiscard]] bool isTransient(const Failure& failure)
{
    return failure.httpStatus == 0 || isTransientStatus(failure.httpStatus);
}

[[nodiscard]] QString failureCategory(const Failure& failure)
{
    if (failure.httpStatus == 0) {
        return QStringLiteral("network");
    }
    return isTransientStatus(failure.httpStatus) ? QStringLiteral("transient") : QStringLiteral("rejected");
}

[[nodiscard]] std::uint32_t retryAfter(const Failure& failure)
{
    return static_cast<std::uint32_t>(std::max(0, failure.retryAfterSeconds.value_or(0)));
}

[[nodiscard]] bool isCancelled(const Failure& failure)
{
    return failure.kind == FailureKind::Cancelled;
}

[[nodiscard]] QStringList idsOf(const QList<db::PlaybackEventRecord>& events)
{
    QStringList ids;
    ids.reserve(events.size());
    for (const db::PlaybackEventRecord& event : events) {
        ids.append(event.eventId);
    }
    return ids;
}

} // namespace

QFuture<MusicSyncService::Outcome> MusicSyncService::historyStep(bool failed, int round)
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    if (round >= kMaxRounds) {
        return ready(Outcome::Failed);
    }
    QList<db::PlaybackEventRecord> events = m_deps.outbox.loadPendingPlaybackEvents(m_context->ownerId, kBatchSize);
    if (events.isEmpty()) {
        scheduleNextDurableRetry(Lane::History, db::AccountRetryQueue::History);
        return ready(failed ? Outcome::Failed : Outcome::Succeeded);
    }
    return uploadHistory(std::move(events))
        .then(this,
              [this, failed, round](Outcome outcome) -> QFuture<Outcome> {
                  if (outcome == Outcome::Unauthorized || outcome == Outcome::Cancelled) {
                      return ready(outcome);
                  }
                  return historyStep(failed || outcome == Outcome::Failed, round + 1);
              })
        .unwrap();
}

QFuture<MusicSyncService::Outcome> MusicSyncService::uploadHistory(QList<db::PlaybackEventRecord> events)
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    const QString owner = m_context->ownerId;
    QList<AccountHistoryEvent> payload;
    QStringList submitted;
    for (const db::PlaybackEventRecord& event : events) {
        std::optional<PortableAccountTrack> track = portableFromQueued(event.trackJson, event.track);
        if (!track || event.eventId.isEmpty() || event.playedAtUtc.isEmpty()) {
            (void)m_deps.outbox.markPlaybackEventsFailed(
                owner, {event.eventId}, {.category = QStringLiteral("invalid-payload"), .blocked = true});
            continue;
        }
        payload.append({event.eventId, std::move(*track), event.playedAtUtc, event.positionSeconds});
        submitted.append(event.eventId);
    }
    if (submitted.isEmpty()) {
        setSafeError(tr("One or more pending history items need attention."));
        return ready(Outcome::Failed);
    }

    return m_deps.transport.postHistoryBatch(m_context->bearerSession, serializeHistoryBatch(payload), submitted)
        .then(this,
              [this, owner, events, submitted](const Result<QStringList>& answer) -> QFuture<Outcome> {
                  if (!isCurrent()) {
                      return ready(Outcome::Cancelled);
                  }
                  if (answer) {
                      const QStringList& acknowledged = answer.value();
                      if (!acknowledged.isEmpty()) {
                          (void)m_deps.outbox.acknowledgePlaybackEvents(owner, acknowledged);
                      }
                      const QSet<QString> accepted(acknowledged.begin(), acknowledged.end());
                      QList<db::PlaybackEventRecord> rejected;
                      for (const db::PlaybackEventRecord& event : events) {
                          if (!accepted.contains(event.eventId)) {
                              rejected.append(event);
                          }
                      }
                      if (rejected.isEmpty()) {
                          return ready(Outcome::Succeeded);
                      }
                      if (rejected.size() == 1) {
                          (void)m_deps.outbox.markPlaybackEventsFailed(
                              owner, {rejected.first().eventId},
                              {.category = QStringLiteral("rejected"), .blocked = true});
                          setSafeError(tr("One pending history item was rejected. Other account data will keep "
                                          "synchronizing."));
                          return ready(Outcome::Failed);
                      }
                      return uploadHalves(std::move(rejected));
                  }

                  const Failure& failure = answer.failure();
                  if (failure.kind == FailureKind::Unauthorized) {
                      return ready(Outcome::Unauthorized);
                  }
                  if (isCancelled(failure)) {
                      return ready(Outcome::Cancelled);
                  }
                  if (isTransient(failure)) {
                      int maxAttempt = 0;
                      for (const db::PlaybackEventRecord& event : events) {
                          maxAttempt = std::max(maxAttempt, event.attemptCount);
                      }
                      const std::chrono::seconds delay = accountRetryDelay(maxAttempt + 1, retryAfter(failure));
                      (void)m_deps.outbox.markPlaybackEventsFailed(
                          owner, submitted,
                          {.category = failure.httpStatus == 0 ? QStringLiteral("network") : QStringLiteral("transient"),
                           .retryDelaySeconds = static_cast<int>(delay.count()),
                           .blocked = false});
                      scheduleRetry(Lane::History, delay);
                      setSafeError(tr("History upload will retry automatically."));
                      return ready(Outcome::Failed);
                  }
                  // Refused: split the batch until the play it refuses is found.
                  if (events.size() > 1) {
                      return uploadHalves(events);
                  }
                  (void)m_deps.outbox.markPlaybackEventsFailed(
                      owner, submitted, {.category = QStringLiteral("rejected"), .blocked = true});
                  setSafeError(
                      tr("One pending history item was rejected. Other account data will keep synchronizing."));
                  return ready(Outcome::Failed);
              })
        .unwrap();
}

QFuture<MusicSyncService::Outcome> MusicSyncService::uploadHalves(QList<db::PlaybackEventRecord> events)
{
    const qsizetype middle = events.size() / 2;
    QList<db::PlaybackEventRecord> right = events.mid(middle);
    events.resize(middle);
    return uploadHistory(std::move(events))
        .then(this,
              [this, right = std::move(right)](Outcome left) mutable -> QFuture<Outcome> {
                  if (left == Outcome::Unauthorized || left == Outcome::Cancelled) {
                      return ready(left);
                  }
                  return uploadHistory(std::move(right)).then([left](Outcome other) {
                      if (other == Outcome::Unauthorized || other == Outcome::Cancelled) {
                          return other;
                      }
                      return left == Outcome::Succeeded && other == Outcome::Succeeded ? Outcome::Succeeded
                                                                                        : Outcome::Failed;
                  });
              })
        .unwrap();
}

QFuture<MusicSyncService::Outcome> MusicSyncService::likesStep(bool failed, int round)
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    if (round >= kMaxRounds) {
        return ready(Outcome::Failed);
    }
    QList<db::PendingLikeRecord> likes = m_deps.outbox.loadPendingLikes(m_context->ownerId, kBatchSize);
    if (likes.isEmpty()) {
        scheduleNextDurableRetry(Lane::Likes, db::AccountRetryQueue::Likes);
        if (failed) {
            setSafeError(tr("Some likes are pending and will retry automatically."));
        }
        return ready(failed ? Outcome::Failed : Outcome::Succeeded);
    }
    auto anyFailed = std::make_shared<bool>(failed);
    return sendLikes(std::move(likes), 0, anyFailed)
        .then(this,
              [this, anyFailed, round](Outcome outcome) -> QFuture<Outcome> {
                  if (outcome != Outcome::Succeeded) {
                      return ready(outcome);
                  }
                  return likesStep(*anyFailed, round + 1);
              })
        .unwrap();
}

QFuture<MusicSyncService::Outcome> MusicSyncService::sendLikes(QList<db::PendingLikeRecord> likes, qsizetype index,
                                                               std::shared_ptr<bool> failed)
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    const QString owner = m_context->ownerId;
    while (index < likes.size()) {
        const db::PendingLikeRecord& like = likes.at(index);
        if (std::optional<PortableAccountTrack> track = portableFromQueued(like.trackJson, like.track)) {
            const QString remoteTrackId = like.remoteTrackId;
            const bool desired = like.desiredState;
            const int attempts = like.attemptCount;
            return m_deps.transport
                .setLiked(m_context->bearerSession, serializePortableTrack(*track), desired, remoteTrackId)
                .then(this,
                      [this, owner, remoteTrackId, desired, attempts, likes = std::move(likes), index,
                       failed](const Result<void>& result) mutable -> QFuture<Outcome> {
                          if (!isCurrent()) {
                              return ready(Outcome::Cancelled);
                          }
                          if (result) {
                              (void)m_deps.outbox.acknowledgePendingLike(owner, remoteTrackId, desired);
                          } else {
                              const Failure& failure = result.failure();
                              if (failure.kind == FailureKind::Unauthorized) {
                                  return ready(Outcome::Unauthorized);
                              }
                              if (isCancelled(failure)) {
                                  return ready(Outcome::Cancelled);
                              }
                              const bool transient = isTransient(failure);
                              const std::chrono::seconds delay = accountRetryDelay(attempts + 1, retryAfter(failure));
                              (void)m_deps.outbox.markPendingLikeFailed(
                                  owner, remoteTrackId,
                                  {.category = failureCategory(failure),
                                   .retryDelaySeconds = transient ? static_cast<int>(delay.count()) : 0,
                                   .blocked = !transient});
                              if (transient) {
                                  scheduleRetry(Lane::Likes, delay);
                              }
                              *failed = true;
                          }
                          return sendLikes(std::move(likes), index + 1, failed);
                      })
                .unwrap();
        }
        (void)m_deps.outbox.markPendingLikeFailed(owner, like.remoteTrackId,
                                                  {.category = QStringLiteral("invalid-payload"), .blocked = true});
        *failed = true;
        ++index;
    }
    return ready(Outcome::Succeeded);
}

QFuture<MusicSyncService::Outcome> MusicSyncService::pushPreferences()
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    const QString owner = m_context->ownerId;
    const QList<db::AccountPreferenceRecord> records =
        m_deps.outbox.loadAccountPreferences(owner, db::OutboxRows::DueOnly);
    if (records.isEmpty()) {
        return ready(Outcome::Succeeded);
    }
    PortablePreferenceValues values;
    QList<std::pair<QString, qint64>> submitted;
    QStringList invalidKeys;
    int maxAttempt = 0;
    for (const db::AccountPreferenceRecord& record : records) {
        const std::optional<PortablePreferenceKey> key = parsePortablePreferenceName(record.key);
        const std::optional<QString> value = key ? validatePortablePreferenceValue(*key, record.valueJson) : std::nullopt;
        if (!key || !value) {
            invalidKeys.append(record.key);
            continue;
        }
        values.insert(*key, *value);
        submitted.append({record.key, record.revision});
        maxAttempt = std::max(maxAttempt, record.attemptCount);
    }
    const bool invalidFailed = !invalidKeys.isEmpty();
    if (invalidFailed) {
        (void)m_deps.outbox.markAccountPreferencesFailed(
            owner, invalidKeys, {.category = QStringLiteral("invalid-payload"), .blocked = true});
    }
    if (values.isEmpty()) {
        setSafeError(tr("One or more preference changes need attention."));
        return ready(Outcome::Failed);
    }

    return m_deps.transport
        .putPreferences(m_context->bearerSession, serializePortablePreferences(portablePreferencesFromValues(values)))
        .then(this, [this, owner, submitted, invalidFailed, maxAttempt](const Result<PortablePreferences>& result) {
            if (!isCurrent()) {
                return Outcome::Cancelled;
            }
            if (result) {
                for (const auto& [key, revision] : submitted) {
                    (void)m_deps.outbox.acknowledgeAccountPreference(owner, key, revision);
                }
                scheduleNextDurableRetry(Lane::PreferencePush, db::AccountRetryQueue::Preferences);
                if (invalidFailed) {
                    setSafeError(tr("One or more preference changes need attention."));
                    return Outcome::Failed;
                }
                return Outcome::Succeeded;
            }
            const Failure& failure = result.failure();
            if (failure.kind == FailureKind::Unauthorized) {
                return Outcome::Unauthorized;
            }
            if (isCancelled(failure)) {
                return Outcome::Cancelled;
            }
            const bool transient = isTransient(failure);
            const std::chrono::seconds delay = accountRetryDelay(maxAttempt + 1, retryAfter(failure));
            QStringList keys;
            for (const auto& entry : submitted) {
                keys.append(entry.first);
            }
            (void)m_deps.outbox.markAccountPreferencesFailed(
                owner, keys,
                {.category = failureCategory(failure),
                 .retryDelaySeconds = transient ? static_cast<int>(delay.count()) : 0,
                 .blocked = !transient});
            if (transient) {
                scheduleRetry(Lane::PreferencePush, delay);
            }
            scheduleNextDurableRetry(Lane::PreferencePush, db::AccountRetryQueue::Preferences);
            setSafeError(tr("Some preference changes are still pending."));
            return Outcome::Failed;
        });
}

QFuture<MusicSyncService::Outcome> MusicSyncService::pullPreferences()
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    const QString owner = m_context->ownerId;
    QHash<QString, db::AccountPreferenceRecord> observed;
    for (const db::AccountPreferenceRecord& record : m_deps.outbox.loadAccountPreferences(owner, db::OutboxRows::All)) {
        observed.insert(record.key, record);
    }

    return m_deps.transport.fetchPreferences(m_context->bearerSession)
        .then(this, [this, owner, observed](const Result<PortablePreferences>& result) {
            if (!isCurrent()) {
                return Outcome::Cancelled;
            }
            if (!result) {
                const Failure& failure = result.failure();
                if (failure.kind == FailureKind::Unauthorized) {
                    return Outcome::Unauthorized;
                }
                if (isCancelled(failure)) {
                    return Outcome::Cancelled;
                }
                if (isTransient(failure)) {
                    scheduleRetry(Lane::PreferencePull,
                                  accountRetryDelay(recordLaneFailure(Lane::PreferencePull), retryAfter(failure)));
                }
                setSafeError(tr("Cloud preferences could not be refreshed. Cached values remain active."));
                return Outcome::Failed;
            }
            const PortablePreferenceValues remote = portablePreferenceValues(result.value());
            for (const PortablePreferenceKey key :
                 {PortablePreferenceKey::Theme, PortablePreferenceKey::Accent, PortablePreferenceKey::GaplessPlayback,
                  PortablePreferenceKey::DataSaver, PortablePreferenceKey::Storefront}) {
                const QString name = portablePreferenceName(key);
                const auto local = observed.constFind(name);
                const auto value = remote.constFind(key);
                if (value == remote.constEnd()) {
                    // A cached row also marks the key as already sent once:
                    // sending every missing key on every pull would never end.
                    if (local == observed.constEnd()) {
                        if (const std::optional<QString> localValue =
                                validatePortablePreferenceValue(key, localPreferenceValue(m_deps.settings, key))) {
                            (void)m_deps.outbox.savePendingAccountPreference(owner, name, *localValue);
                            scheduleMutation(Lane::PreferencePush);
                        }
                    }
                    continue;
                }
                if (local != observed.constEnd() && local->pending) {
                    continue; // the change made here wins until it is sent
                }
                const qint64 revision = local == observed.constEnd() ? 0 : local->revision;
                (void)m_deps.outbox.mergeRemoteAccountPreference(owner, name, value.value(), revision);
            }

            m_applyingRemote = true;
            for (const db::AccountPreferenceRecord& record :
                 m_deps.outbox.loadAccountPreferences(owner, db::OutboxRows::All)) {
                if (record.pending) {
                    continue;
                }
                if (const std::optional<PortablePreferenceKey> key = parsePortablePreferenceName(record.key)) {
                    applyPortablePreference(m_deps.settings, *key, record.valueJson);
                }
            }
            m_applyingRemote = false;
            resetLaneFailure(Lane::PreferencePull);
            ++m_preferenceRevision;
            emit preferencesChanged();
            return Outcome::Succeeded;
        });
}

QFuture<MusicSyncService::Outcome> MusicSyncService::pullLibrary()
{
    if (!isCurrent()) {
        return ready(Outcome::Cancelled);
    }
    return m_deps.transport.fetchLibrary(m_context->bearerSession)
        .then(this, [this](const Result<ParsedAccountLibrary>& result) {
            if (!isCurrent()) {
                return Outcome::Cancelled;
            }
            if (!result) {
                const Failure& failure = result.failure();
                if (failure.kind == FailureKind::Unauthorized) {
                    return Outcome::Unauthorized;
                }
                if (isCancelled(failure)) {
                    return Outcome::Cancelled;
                }
                if (isTransient(failure)) {
                    scheduleRetry(Lane::Library,
                                  accountRetryDelay(recordLaneFailure(Lane::Library), retryAfter(failure)));
                }
                setSafeError(tr("The account library could not be refreshed. Cached data remains available."));
                saveSyncError(QStringLiteral("library-request"));
                return Outcome::Failed;
            }
            if (!result.value().valid) {
                setSafeError(tr("The account library response was not accepted."));
                saveSyncError(QStringLiteral("library-response"));
                return Outcome::Failed;
            }

            const QString owner = m_context->ownerId;
            AccountLibraryInputs inputs;
            inputs.ownerId = owner;
            inputs.profile = m_deps.session.profile();
            inputs.nowUtc = m_deps.nowUtc();
            inputs.existing = m_deps.accounts.loadAccountLibrary(owner);
            inputs.pendingLikes = m_deps.outbox.loadPendingLikes(owner, kAllRows, db::OutboxRows::All);
            inputs.pendingEvents = m_deps.outbox.loadPendingPlaybackEvents(owner, kAllRows, db::OutboxRows::All);
            const db::AccountLibrarySnapshot snapshot = buildAccountLibrarySnapshot(result.value(), inputs);
            if (!isCurrent() || !m_deps.accounts.replaceAccountLibrary(owner, snapshot)) {
                setSafeError(tr("Could not save the synchronized account library."));
                saveSyncError(QStringLiteral("cache-write"));
                return Outcome::Failed;
            }
            resetLaneFailure(Lane::Library);
            m_homeLayout = result.value().homeLayout;
            m_homeLayoutOwner = owner;
            ++m_libraryRevision;
            emit libraryChanged();
            return Outcome::Succeeded;
        });
}

} // namespace lmp::core::account
