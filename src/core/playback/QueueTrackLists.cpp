#include "core/playback/QueueTrackLists.h"

#include "core/playback/QueueTrackIdentity.h"

#include <algorithm>

namespace lmp::core {

QList<Track> uniqueQueueTracks(const QList<Track>& tracks, QSet<QString> seen)
{
    QList<Track> unique;
    unique.reserve(tracks.size());
    for (const Track& track : tracks) {
        const QString identity = queueTrackIdentity(track);
        if (identity.isEmpty() || seen.contains(identity)) {
            continue;
        }
        seen.insert(identity);
        unique.append(track);
    }
    return unique;
}

QSet<QString> queueIdentitiesOf(const QList<Track>& tracks)
{
    QSet<QString> identities;
    identities.reserve(tracks.size());
    for (const Track& track : tracks) {
        identities.insert(queueTrackIdentity(track));
    }
    return identities;
}

QList<Track> queueTracksOf(const QList<QueueEntry>& entries)
{
    QList<Track> tracks;
    tracks.reserve(entries.size());
    for (const QueueEntry& entry : entries) {
        tracks.append(entry.track);
    }
    return tracks;
}

QList<Track> slicedQueueTracks(const QList<Track>& list, qint64 start, qint64 end)
{
    const qint64 length = list.size();
    const auto normalize = [length](qint64 value) {
        return value < 0 ? std::max<qint64>(0, length + value) : std::min(value, length);
    };
    const qint64 from = normalize(start);
    const qint64 to = normalize(end);
    if (to <= from) {
        return {};
    }
    return list.mid(static_cast<qsizetype>(from), static_cast<qsizetype>(to - from));
}

QList<qsizetype> canonicalQueueOrder(qsizetype length)
{
    QList<qsizetype> order;
    order.reserve(std::max<qsizetype>(0, length));
    for (qsizetype index = 0; index < length; ++index) {
        order.append(index);
    }
    return order;
}

} // namespace lmp::core
