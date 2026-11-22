#pragma once

#include "core/model/Track.h"
#include "core/playback/QueueTypes.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QtGlobal>

namespace lmp::core {

// List operations the queue engine shares between its files. Identities are
// those of queueTrackIdentity.

// Keeps the first copy of each song and drops songs without an identity or
// whose identity is already in `seen`.
[[nodiscard]] QList<Track> uniqueQueueTracks(const QList<Track>& tracks, QSet<QString> seen = {});

[[nodiscard]] QSet<QString> queueIdentitiesOf(const QList<Track>& tracks);

// The tracks of visible queue rows, in row order.
[[nodiscard]] QList<Track> queueTracksOf(const QList<QueueEntry>& entries);

// Array.prototype.slice semantics: a negative bound counts from the end, and
// bounds past either end are clamped.
[[nodiscard]] QList<Track> slicedQueueTracks(const QList<Track>& list, qint64 start, qint64 end);

// 0 .. length-1, the play order of an unshuffled context.
[[nodiscard]] QList<qsizetype> canonicalQueueOrder(qsizetype length);

[[nodiscard]] constexpr bool isQueueIndexInRange(qsizetype index, qsizetype size) noexcept
{
    return index >= 0 && index < size;
}

} // namespace lmp::core
