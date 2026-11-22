#pragma once

#include "core/model/Track.h"

#include <QJsonObject>
#include <QList>
#include <QtGlobal>

#include <limits>

namespace lmp::core {

// The queue as it will play, saved across restarts. A shuffled context is
// stored in play order, so a restore continues with the same songs instead of
// shuffling again. The user queue is stored as it is.
struct QueueSnapshot {
    QList<Track> contextQueue;
    // The current song in contextQueue, or -1 when none is current.
    qsizetype contextIndex = -1;
    QList<Track> userQueue;

    // {"contextQueue": [...], "contextIndex": n, "userQueue": [...]}, each
    // song in the form Track::toSnapshotJson writes.
    [[nodiscard]] QJsonObject toJson() const;
    // Never fails: a list that is missing or not an array reads as empty,
    // entries that are not objects are skipped, an index that is missing or
    // not a whole number reads as -1, and a huge one is clamped to the int
    // range. The queue clamps the index into its context when it is restored.
    [[nodiscard]] static QueueSnapshot fromJson(const QJsonObject& object);

    friend bool operator==(const QueueSnapshot&, const QueueSnapshot&) = default;
};

// How much of the queue a snapshot keeps, counted from the current song:
// played songs before it, upcoming songs after it, and the user queue.
struct QueueSnapshotLimits {
    // Large enough to mean "everything", small enough that adding a queue
    // position to it cannot overflow.
    static constexpr qint64 Unlimited = std::numeric_limits<qint64>::max() / 4;

    qint64 played = Unlimited;
    qint64 upcoming = Unlimited;
    qint64 user = Unlimited;
};

} // namespace lmp::core
