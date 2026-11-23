#include "core/playback/QueueEngine.h"

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTrackLists.h"

#include <QSet>
#include <QString>

#include <algorithm>
#include <utility>

// The edits of the visible queue. The rest of QueueEngine is in
// QueueEngine.cpp.

namespace lmp::core {

QueueEngine QueueEngine::removeVisibleEntry(qsizetype visibleIndex) const
{
    QList<QueueEntry> visible = visibleQueue();
    if (!isQueueIndexInRange(visibleIndex, visible.size())) {
        return *this;
    }
    const QueueEntry entry = visible.takeAt(visibleIndex);
    if (entry.kind == QueueEntryKind::User) {
        QueueState next = m_state;
        next.userQueue.removeAt(entry.sourceIndex);
        return QueueEngine(std::move(next));
    }
    return commitVisibleOrder(queueTracksOf(visible));
}

QueueEngine QueueEngine::restoreVisibleEntry(qsizetype visibleIndex, const QueueEntry& entry) const
{
    const QString identity = queueTrackIdentity(entry.track);
    if (identity.isEmpty() || queueIdentitiesOf(m_state.contextQueue + m_state.userQueue).contains(identity)) {
        return *this;
    }
    if (entry.kind == QueueEntryKind::User) {
        QueueState next = m_state;
        next.userQueue.insert(std::clamp<qsizetype>(visibleIndex, 0, next.userQueue.size()), entry.track);
        return QueueEngine(std::move(next));
    }
    QList<Track> restored = queueTracksOf(visibleQueue());
    // Never inside the user queue, never past the end.
    const qsizetype position = std::max(m_state.userQueue.size(), std::min(visibleIndex, restored.size()));
    restored.insert(position, entry.track);
    return commitVisibleOrder(restored);
}

QueueEngine QueueEngine::moveVisibleEntry(qsizetype visibleIndex, QueueDirection delta) const
{
    const QList<QueueEntry> visible = visibleQueue();
    const qsizetype step = delta == QueueDirection::Next ? 1 : -1;
    const qsizetype target = std::max<qsizetype>(0, std::min(visible.size() - 1, visibleIndex + step));
    if (!isQueueIndexInRange(visibleIndex, visible.size()) || target == visibleIndex) {
        return *this;
    }
    QList<Track> reordered = queueTracksOf(visible);
    reordered.move(visibleIndex, target);
    return commitVisibleOrder(reordered);
}

QueueEngine QueueEngine::commitVisibleOrder(const QList<Track>& visibleTracks) const
{
    const qsizetype userCount = std::min(m_state.userQueue.size(), visibleTracks.size());
    const QList<Track> userQueue = uniqueQueueTracks(visibleTracks.first(userCount));
    const std::optional<Track> current = currentContextTrack();
    QList<Track> future;
    for (const Track& track : uniqueQueueTracks(visibleTracks.sliced(userCount))) {
        if (!current || queueTrackIdentity(track) != queueTrackIdentity(*current)) {
            future.append(track);
        }
    }

    // Played songs keep their place before the current one, except those the
    // new order moved ahead. With repeat-all they all come round again, so
    // the visible list already holds every one of them.
    QList<Track> prior;
    if (m_state.repeatMode != RepeatMode::All) {
        const QSet<QString> moved = queueIdentitiesOf(userQueue + future);
        for (const Track& track : slicedQueueTracks(m_state.contextQueue, 0, std::max<qsizetype>(0, m_state.contextIndex))) {
            if (!moved.contains(queueTrackIdentity(track))) {
                prior.append(track);
            }
        }
    }
    const qsizetype contextIndex = current ? prior.size() : -1;
    QList<Track> contextQueue = current ? prior + QList<Track>{*current} + future : future;

    QueueEngine rebuilt = create(contextQueue, contextIndex, userQueue, Shuffle::Off, m_state.repeatMode);
    rebuilt.m_state.shuffle = m_state.shuffle;
    if (m_state.shuffle) {
        // The order just committed is the play order from now on.
        rebuilt.m_state.shuffleOrder = canonicalQueueOrder(rebuilt.m_state.contextQueue.size());
    }
    return rebuilt;
}

QList<Track> QueueEngine::mergeRemoteUpcoming(const QList<Track>& visible, const QList<Track>& desired,
                                              const QSet<QString>& seenIdentities)
{
    if (desired.isEmpty()) {
        return {};
    }
    QSet<QString> listed;
    for (const Track& track : desired) {
        listed.insert(queueTrackIdentity(track));
    }
    QList<Track> merged = desired;
    for (const Track& track : visible) {
        const QString identity = queueTrackIdentity(track);
        if (!seenIdentities.contains(identity) && !listed.contains(identity)) {
            merged.append(track);
        }
    }
    return merged;
}

} // namespace lmp::core
