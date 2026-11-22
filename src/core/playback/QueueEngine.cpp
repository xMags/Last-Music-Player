#include "core/playback/QueueEngine.h"

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTrackLists.h"

#include <QSet>
#include <QString>

#include <algorithm>
#include <utility>

namespace lmp::core {

QueueEngine::QueueEngine(QueueState state)
    : m_state(std::move(state))
{
}

QueueEngine QueueEngine::create(const QList<Track>& contextQueue, qsizetype contextIndex,
                                const QList<Track>& userQueue, Shuffle shuffle, RepeatMode repeatMode,
                                const QueueRandom& random)
{
    QueueState state;
    state.contextQueue = uniqueQueueTracks(contextQueue);
    const qsizetype size = state.contextQueue.size();
    state.contextIndex = size > 0 ? std::clamp<qsizetype>(contextIndex, 0, size - 1) : -1;
    state.userQueue = uniqueQueueTracks(userQueue);
    state.shuffle = shuffle == Shuffle::On;
    if (state.shuffle) {
        state.shuffleOrder = anchoredShuffleOrder(size, state.contextIndex, random);
    }
    state.repeatMode = repeatMode;
    return QueueEngine(std::move(state));
}

QueueEngine QueueEngine::fromState(QueueState state)
{
    if (!isPermutation(state.shuffleOrder, state.contextQueue.size())) {
        state.shuffleOrder.clear();
    }
    return QueueEngine(std::move(state));
}

QueueEngine QueueEngine::restored(const QueueSnapshot& snapshot, Shuffle shuffle, RepeatMode repeatMode)
{
    QueueEngine queue = create(snapshot.contextQueue, snapshot.contextIndex, snapshot.userQueue, Shuffle::Off,
                               repeatMode);
    queue.m_state.shuffle = shuffle == Shuffle::On;
    if (queue.m_state.shuffle) {
        queue.m_state.shuffleOrder = canonicalQueueOrder(queue.m_state.contextQueue.size());
    }
    return queue;
}

std::optional<Track> QueueEngine::currentContextTrack() const
{
    if (!isQueueIndexInRange(m_state.contextIndex, m_state.contextQueue.size())) {
        return std::nullopt;
    }
    return m_state.contextQueue.at(m_state.contextIndex);
}

QList<qsizetype> QueueEngine::playOrder() const
{
    const qsizetype size = m_state.contextQueue.size();
    return m_state.shuffle && m_state.shuffleOrder.size() == size ? m_state.shuffleOrder : canonicalQueueOrder(size);
}

QList<QueueEntry> QueueEngine::visibleQueue() const
{
    QList<QueueEntry> entries;
    for (qsizetype index = 0; index < m_state.userQueue.size(); ++index) {
        entries.append(QueueEntry{m_state.userQueue.at(index), QueueEntryKind::User, index});
    }
    const QList<Track>& context = m_state.contextQueue;
    if (context.isEmpty()) {
        return entries;
    }
    const QList<qsizetype> order = playOrder();
    const auto appendContext = [&entries, &context](qsizetype sourceIndex) {
        entries.append(QueueEntry{context.at(sourceIndex), QueueEntryKind::Context, sourceIndex});
    };
    if (!isQueueIndexInRange(m_state.contextIndex, context.size())) {
        for (const qsizetype sourceIndex : order) {
            appendContext(sourceIndex);
        }
        return entries;
    }
    const qsizetype currentPosition = std::max<qsizetype>(0, order.indexOf(m_state.contextIndex));
    if (m_state.repeatMode == RepeatMode::All) {
        for (qsizetype offset = 1; offset < context.size(); ++offset) {
            appendContext(order.at((currentPosition + offset) % order.size()));
        }
    } else {
        for (qsizetype position = currentPosition + 1; position < order.size(); ++position) {
            appendContext(order.at(position));
        }
    }
    return entries;
}

qsizetype QueueEngine::upcomingCount() const
{
    const qsizetype size = m_state.contextQueue.size();
    const qsizetype userCount = m_state.userQueue.size();
    if (m_state.shuffle && m_state.shuffleOrder.size() == size) {
        const qsizetype position = std::max<qsizetype>(0, m_state.shuffleOrder.indexOf(m_state.contextIndex));
        return userCount + std::max<qsizetype>(0, size - 1 - position);
    }
    return userCount + std::max<qsizetype>(0, size - 1 - m_state.contextIndex);
}

QueueEngine QueueEngine::replaceContext(const QList<Track>& tracks, qsizetype selectedIndex,
                                        const QueueRandom& random) const
{
    const Shuffle shuffle = m_state.shuffle ? Shuffle::On : Shuffle::Off;
    QueueEngine next = create(tracks, selectedIndex, m_state.userQueue, shuffle, m_state.repeatMode, random);
    // The user queue is carried over as it was, not de-duplicated again.
    next.m_state.userQueue = m_state.userQueue;
    return next;
}

QueueAdvance QueueEngine::advance(QueueDirection direction, AdvanceTrigger trigger, const QueueRandom& random) const
{
    const bool automatic = trigger == AdvanceTrigger::Automatic;
    if (automatic && m_state.repeatMode == RepeatMode::One) {
        if (std::optional<Track> current = currentContextTrack()) {
            return QueueAdvance{*this, std::move(current), false};
        }
    }
    QueueState next = m_state;
    if (direction == QueueDirection::Next && !next.userQueue.isEmpty()) {
        Track track = next.userQueue.takeFirst();
        return QueueAdvance{QueueEngine(std::move(next)), std::move(track), false};
    }
    const qsizetype size = next.contextQueue.size();
    if (size == 0) {
        return QueueAdvance{QueueEngine(std::move(next)), std::nullopt, true};
    }

    const qsizetype step = direction == QueueDirection::Next ? 1 : -1;
    const bool stopAtEnd = automatic && next.repeatMode == RepeatMode::Off;
    qsizetype nextIndex = next.contextIndex;
    if (next.shuffle && size > 1) {
        if (next.shuffleOrder.size() != size) {
            next.shuffleOrder = anchoredShuffleOrder(size, next.contextIndex, random);
        }
        const qsizetype position =
            next.contextIndex >= 0 ? std::max<qsizetype>(0, next.shuffleOrder.indexOf(next.contextIndex)) : -1;
        qsizetype target = position + step;
        if (target >= size) {
            if (stopAtEnd) {
                return QueueAdvance{QueueEngine(std::move(next)), std::nullopt, true};
            }
            target = 0;
        } else if (target < 0) {
            target = size - 1;
        }
        nextIndex = next.shuffleOrder.at(target);
    } else {
        nextIndex += step;
        if (nextIndex >= size) {
            if (stopAtEnd) {
                return QueueAdvance{QueueEngine(std::move(next)), std::nullopt, true};
            }
            nextIndex = 0;
        } else if (nextIndex < 0) {
            nextIndex = size - 1;
        }
    }
    next.contextIndex = nextIndex;
    Track track = next.contextQueue.at(nextIndex);
    return QueueAdvance{QueueEngine(std::move(next)), std::move(track), false};
}

QueueEngine QueueEngine::insertPlayNext(const QList<Track>& tracks) const
{
    QueueState next = m_state;
    const QList<Track> incoming = uniqueQueueTracks(tracks);
    const QSet<QString> incomingIdentities = queueIdentitiesOf(incoming);
    // A song that is already queued moves to the front instead of the
    // request doing nothing.
    QList<Track> userQueue = incoming;
    for (const Track& track : m_state.userQueue) {
        if (!incomingIdentities.contains(queueTrackIdentity(track))) {
            userQueue.append(track);
        }
    }
    next.userQueue = std::move(userQueue);
    return QueueEngine(std::move(next));
}

QueueEngine QueueEngine::appendUserQueue(const QList<Track>& tracks) const
{
    QueueState next = m_state;
    next.userQueue.append(uniqueQueueTracks(tracks, queueIdentitiesOf(m_state.userQueue)));
    return QueueEngine(std::move(next));
}

QueueEngine QueueEngine::withShuffle(Shuffle shuffle, const QueueRandom& random) const
{
    QueueState next = m_state;
    if (shuffle == Shuffle::On) {
        next.shuffle = true;
        next.shuffleOrder = anchoredShuffleOrder(next.contextQueue.size(), next.contextIndex, random);
        return QueueEngine(std::move(next));
    }
    next.shuffle = false;
    next.shuffleOrder.clear();
    if (!m_state.shuffle || m_state.shuffleOrder.size() != m_state.contextQueue.size()) {
        return QueueEngine(std::move(next));
    }
    // The shuffled traversal becomes the list order, which keeps both what
    // was played and everything still to come where it was instead of
    // jumping back to the original order.
    next.contextIndex = m_state.contextIndex >= 0
        ? std::max<qsizetype>(0, m_state.shuffleOrder.indexOf(m_state.contextIndex))
        : -1;
    next.contextQueue.clear();
    for (const qsizetype index : m_state.shuffleOrder) {
        next.contextQueue.append(m_state.contextQueue.at(index));
    }
    return QueueEngine(std::move(next));
}

QueueEngine QueueEngine::withRepeatMode(RepeatMode repeatMode) const
{
    QueueState next = m_state;
    next.repeatMode = repeatMode;
    return QueueEngine(std::move(next));
}

QueueEngine QueueEngine::appendAutoplayTracks(const QList<Track>& tracks) const
{
    const QSet<QString> existing = queueIdentitiesOf(m_state.contextQueue + m_state.userQueue);
    QList<Track> appended;
    for (const Track& track : uniqueQueueTracks(tracks)) {
        if (appended.size() == MaximumAutoplayTracks) {
            break;
        }
        if (!existing.contains(queueTrackIdentity(track))) {
            appended.append(track);
        }
    }
    if (appended.isEmpty()) {
        return *this;
    }
    QueueState next = m_state;
    const qsizetype before = next.contextQueue.size();
    next.contextQueue.append(appended);
    if (next.shuffle && next.shuffleOrder.size() == before) {
        for (qsizetype offset = 0; offset < appended.size(); ++offset) {
            next.shuffleOrder.append(before + offset);
        }
    }
    return QueueEngine(std::move(next));
}
QueueSnapshot QueueEngine::snapshot(const QueueSnapshotLimits& limits) const
{
    const QList<qsizetype> order = playOrder();
    QList<Track> inPlayOrder;
    inPlayOrder.reserve(order.size());
    for (const qsizetype index : order) {
        inPlayOrder.append(m_state.contextQueue.at(index));
    }
    QueueSnapshot result;
    result.userQueue = slicedQueueTracks(m_state.userQueue, 0, limits.user);
    if (!isQueueIndexInRange(m_state.contextIndex, m_state.contextQueue.size())) {
        result.contextQueue = slicedQueueTracks(inPlayOrder, 0, limits.upcoming);
        result.contextIndex = -1;
        return result;
    }
    const qint64 position = std::max<qsizetype>(0, order.indexOf(m_state.contextIndex));
    const qint64 start = std::max<qint64>(0, position - limits.played);
    result.contextQueue = slicedQueueTracks(inPlayOrder, start, position + 1 + limits.upcoming);
    result.contextIndex = static_cast<qsizetype>(position - start);
    return result;
}

} // namespace lmp::core
