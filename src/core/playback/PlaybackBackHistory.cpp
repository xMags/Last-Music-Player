#include "core/playback/PlaybackBackHistory.h"

#include <algorithm>

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTrackLists.h"

#include <utility>

namespace lmp::core {

void PlaybackBackHistory::remember(const std::optional<Track>& playing, const QueueEngine& queue,
                                   const std::optional<Track>& takenFromUserQueue)
{
    if (!playing) {
        return;
    }
    const std::optional<Track> contextTrack = queue.currentContextTrack();
    m_entries.append(Entry{*playing, contextTrack ? queueTrackIdentity(*contextTrack) : QString(),
                           takenFromUserQueue});
    if (m_entries.size() > Capacity) {
        m_entries.removeFirst();
    }
}

std::optional<PlaybackBackHistory::Restored> PlaybackBackHistory::restorePrevious(const QueueEngine& queue)
{
    if (m_entries.isEmpty()) {
        return std::nullopt;
    }
    Entry previous = m_entries.takeLast();
    QueueEngine restored = queue;
    if (!previous.contextIdentity.isEmpty()) {
        QueueState state = queue.state();
        qsizetype index = -1;
        for (qsizetype position = 0; position < state.contextQueue.size(); ++position) {
            if (queueTrackIdentity(state.contextQueue.at(position)) == previous.contextIdentity) {
                index = position;
                break;
            }
        }
        if (index >= 0) {
            state.contextIndex = index;
        } else {
            // The context changed since: the song goes back at its front, and
            // the rest follow in the order they were going to play (from the
            // song that was on), so a shuffled context stays shuffled.
            const QString identity = queueTrackIdentity(previous.track);
            QList<Track> context{previous.track};
            const qsizetype size = state.contextQueue.size();
            const QList<qsizetype> order = state.shuffle && state.shuffleOrder.size() == size
                                               ? state.shuffleOrder
                                               : canonicalQueueOrder(size);
            const qsizetype count = order.size();
            const qsizetype start =
                isQueueIndexInRange(state.contextIndex, count) ? std::max<qsizetype>(0, order.indexOf(state.contextIndex)) : 0;
            for (qsizetype offset = 0; offset < count; ++offset) {
                const Track& track = state.contextQueue.at(order.at((start + offset) % count));
                if (queueTrackIdentity(track) != identity) {
                    context.append(track);
                }
            }
            state.contextQueue = std::move(context);
            state.contextIndex = 0;
            state.shuffleOrder = state.shuffle ? canonicalQueueOrder(state.contextQueue.size()) : QList<qsizetype>();
        }
        restored = QueueEngine::fromState(std::move(state));
    }
    if (previous.replayTrack) {
        restored = restored.insertPlayNext({*previous.replayTrack});
    }
    return Restored{std::move(restored), std::move(previous.track)};
}

} // namespace lmp::core
