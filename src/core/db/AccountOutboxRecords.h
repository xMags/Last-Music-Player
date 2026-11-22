#pragma once

#include "core/model/Track.h"

#include <QString>
#include <QtGlobal>

#include <optional>

// The values of the account outbox: changes made on this PC that wait to be
// sent to the account service, with their retry bookkeeping.
namespace lmp::core::db {

struct PlaybackEventRecord {
    QString eventId;
    QString accountId;
    QString remoteTrackId;
    // Required to enqueue. Read back from trackJson, and empty when that
    // text does not parse as an object.
    std::optional<Track> track;
    QString playedAtUtc;
    double positionSeconds = 0;
    int attemptCount = 0;
    // As stored; filled when read.
    QString trackJson;
    // Unix seconds; 0 means due now.
    qint64 nextAttemptAtUnix = 0;
    QString lastFailureCategory;
    bool blocked = false;
};

struct PendingLikeRecord {
    QString accountId;
    QString remoteTrackId;
    // Required to enqueue; see PlaybackEventRecord::track.
    std::optional<Track> track;
    bool desiredState = false;
    QString updatedAtUtc;
    int attemptCount = 0;
    QString trackJson;
    qint64 nextAttemptAtUnix = 0;
    QString lastFailureCategory;
    bool blocked = false;
};

struct AccountPreferenceRecord {
    QString accountId;
    QString key;
    QString valueJson;
    qint64 revision = 0;
    int attemptCount = 0;
    qint64 nextAttemptAtUnix = 0;
    QString lastFailureCategory;
    bool pending = false;
    bool blocked = false;
};

// Rows waiting to be sent ("pending") and rows the service refused for good
// ("blocked"), per queue.
struct AccountPendingCounts {
    int pendingHistory = 0;
    int blockedHistory = 0;
    int pendingLikes = 0;
    int blockedLikes = 0;
    int pendingPreferences = 0;
    int blockedPreferences = 0;
};

enum class AccountRetryQueue {
    History,
    Likes,
    Preferences,
};

// Which rows a load returns.
enum class OutboxRows {
    // Not blocked and due: the retry time has come.
    DueOnly,
    All,
};

// How a failed send is recorded.
struct OutboxFailureMark {
    QString category;
    // From now; negative counts as 0. Ignored when blocked.
    int retryDelaySeconds = 0;
    // Refused for good: kept, but never retried until the schedule is reset.
    bool blocked = false;
};

} // namespace lmp::core::db
