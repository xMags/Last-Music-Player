#pragma once

#include "core/async/Result.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/db/Database.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstddef>
#include <optional>

namespace lmp::core::db {

// Changes made on this PC that wait to be sent to the account service:
// playback history, like and unlike requests, and preference values, each
// with its retry bookkeeping. Rows belong to one account id; signing out
// removes them with the account's profile.
//
// "Due" rows are not blocked and their NextAttemptAtUnix has come
// (preferences must also still be pending). Retry times are Unix seconds on
// SQLite's own clock, unixepoch(); attempt times are datetime('now') text.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; that Database must outlive it.
//
// An operation that earlier versions reported as false or 0 returns a
// Failure: FailureKind::NotFound when there was nothing to act on (an empty
// id or key, no song, a revision that is not positive), FailureKind::Storage
// when SQLite refused. Updates that match no row still succeed, as before.
class AccountOutboxRepository {
public:
    explicit AccountOutboxRepository(Database& database);

    // Keeps the first event with a given id. Makes the account's profile.
    [[nodiscard]] Result<void> enqueuePlaybackEvent(const PlaybackEventRecord& event);
    // Oldest first; none for a limit of 0 or less.
    [[nodiscard]] QList<PlaybackEventRecord> loadPendingPlaybackEvents(const QString& accountId,
                                                                       int limit = 100,
                                                                       OutboxRows rows = OutboxRows::DueOnly) const;
    // Each in one transaction over the listed ids.
    [[nodiscard]] Result<void> markPlaybackEventsAttempted(const QString& accountId, const QStringList& eventIds);
    [[nodiscard]] Result<void> markPlaybackEventsFailed(const QString& accountId,
                                                        const QStringList& eventIds,
                                                        const OutboxFailureMark& failure);
    [[nodiscard]] Result<void> acknowledgePlaybackEvents(const QString& accountId, const QStringList& eventIds);
    // Plays that earlier versions queued with a braced id ("{GUID}") and the
    // service refused for that alone are queued again; they upload with the
    // braces taken off (nothing else about them changes).
    [[nodiscard]] Result<void> releaseBracedPlaybackEvents(const QString& accountId);

    // One row per song: a newer request replaces the waiting one and starts
    // its retries over. Makes the account's profile.
    [[nodiscard]] Result<void> enqueuePendingLike(const PendingLikeRecord& like);
    // Oldest first; none for a limit of 0 or less.
    [[nodiscard]] QList<PendingLikeRecord> loadPendingLikes(const QString& accountId,
                                                            int limit = 100,
                                                            OutboxRows rows = OutboxRows::DueOnly) const;
    [[nodiscard]] Result<void> markPendingLikeAttempted(const QString& accountId, const QString& remoteTrackId);
    [[nodiscard]] Result<void> markPendingLikeFailed(const QString& accountId,
                                                     const QString& remoteTrackId,
                                                     const OutboxFailureMark& failure);
    // Removes the request only if it still asks for `desiredState`, so a
    // newer opposite request survives the reply to an older one.
    [[nodiscard]] Result<void> acknowledgePendingLike(const QString& accountId,
                                                      const QString& remoteTrackId,
                                                      bool desiredState);

    // By key.
    [[nodiscard]] QList<AccountPreferenceRecord> loadAccountPreferences(const QString& accountId,
                                                                        OutboxRows rows = OutboxRows::All) const;
    // Stores a value changed here as pending, raising its revision (1 for a
    // new key) and starting retries over. Returns the new revision.
    [[nodiscard]] Result<qint64> savePendingAccountPreference(const QString& accountId,
                                                              const QString& key,
                                                              const QString& valueJson);
    // Takes a value from the service, unless a local change is pending or
    // the revision moved on since `observedRevision` was read. A new key is
    // stored at revision 0. Succeeds either way.
    [[nodiscard]] Result<void> mergeRemoteAccountPreference(const QString& accountId,
                                                            const QString& key,
                                                            const QString& valueJson,
                                                            qint64 observedRevision);
    // Clears the pending flag if the stored revision is still `revision`.
    [[nodiscard]] Result<void> acknowledgeAccountPreference(const QString& accountId,
                                                            const QString& key,
                                                            qint64 revision);
    // Pending keys only, in one transaction.
    [[nodiscard]] Result<void> markAccountPreferencesFailed(const QString& accountId,
                                                            const QStringList& keys,
                                                            const OutboxFailureMark& failure);

    // All zero for an empty id or when the count fails.
    [[nodiscard]] AccountPendingCounts loadAccountPendingCounts(const QString& accountId) const;
    // Seconds (at least 1) until the earliest waiting retry of the queue, or
    // nothing when none waits.
    [[nodiscard]] std::optional<int> secondsUntilNextAccountRetry(const QString& accountId,
                                                                  AccountRetryQueue queue) const;
    // Makes every row of the account due now and unblocks it.
    [[nodiscard]] Result<void> resetAccountRetrySchedule(const QString& accountId);
    // Makes every row that is not blocked due now; blocked rows stay blocked.
    [[nodiscard]] Result<void> resetRecoverableAccountRetrySchedule(const QString& accountId);
    // Rows that are not blocked, over all three queues.
    [[nodiscard]] std::optional<std::size_t> recoverablePendingChangeCount(const QString& accountId) const;

private:
    // Runs `sql` with ?1 = accountId and ?2 = each id, in one transaction.
    [[nodiscard]] Result<void> updateEach(const char* sql, const QString& accountId, const QStringList& ids);
    [[nodiscard]] Result<void> markEachFailed(const char* sql,
                                              const QString& accountId,
                                              const QStringList& ids,
                                              const OutboxFailureMark& failure);
    // Runs the three statements with ?1 = accountId in one transaction.
    [[nodiscard]] Result<void> resetSchedule(const QString& accountId,
                                             const char* historySql,
                                             const char* likesSql,
                                             const char* preferencesSql);

    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
