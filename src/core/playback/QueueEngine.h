#pragma once

#include "core/model/Track.h"
#include "core/playback/QueueShuffle.h"
#include "core/playback/QueueSnapshot.h"
#include "core/playback/QueueTypes.h"

#include <QList>
#include <QtGlobal>

#include <optional>

namespace lmp::core {

struct QueueAdvance;

// The play queue as a value: every operation is const and returns the new
// queue, leaving this one unchanged, so a caller can compare before and after
// or drop a result. Songs are told apart by queueTrackIdentity; songs without
// an identity and repeated songs are dropped wherever a list comes in.
// Operations that shuffle take the generator to draw from.
//
// No I/O, no signals. A value is safe to read from several threads; it is
// not changed in place.
class QueueEngine final {
public:
    // An empty queue: no context, no user queue, shuffle and repeat off.
    QueueEngine() = default;

    // A fresh queue. Repeated songs are dropped and the index is clamped into
    // the context (-1 when it is empty). With shuffle on, the order is drawn
    // anchored on the current song.
    [[nodiscard]] static QueueEngine create(const QList<Track>& contextQueue = {}, qsizetype contextIndex = -1,
                                            const QList<Track>& userQueue = {}, Shuffle shuffle = Shuffle::Off,
                                            RepeatMode repeatMode = RepeatMode::Off,
                                            const QueueRandom& random = systemQueueRandom());
    // Takes a state as it is. Only a shuffle order that is not a permutation
    // of the context is replaced by an empty one (that is, "not in use"), so
    // no operation can read outside the context.
    [[nodiscard]] static QueueEngine fromState(QueueState state);
    // A queue from a saved snapshot, keeping its saved play order: a shuffled
    // queue continues in that order instead of shuffling again.
    [[nodiscard]] static QueueEngine restored(const QueueSnapshot& snapshot, Shuffle shuffle, RepeatMode repeatMode);

    [[nodiscard]] const QueueState& state() const noexcept { return m_state; }
    // The current song in the context, if the index points into it.
    [[nodiscard]] std::optional<Track> currentContextTrack() const;
    // The songs still to play: the user queue, then the context after the
    // current song in play order. With repeat-all the context wraps round to
    // the song before the current one.
    [[nodiscard]] QList<QueueEntry> visibleQueue() const;
    // How many songs are still to play, without wrapping for repeat-all.
    [[nodiscard]] qsizetype upcomingCount() const;

    // A new context starting at `selectedIndex`. The user queue, shuffle and
    // repeat stay; a shuffled context gets a fresh order.
    [[nodiscard]] QueueEngine replaceContext(const QList<Track>& tracks, qsizetype selectedIndex,
                                             const QueueRandom& random = systemQueueRandom()) const;
    // One step. Going forward the user queue comes first. A manual step wraps
    // round at either end; an automatic one ends the queue at the end unless
    // repeat-all is on, and replays the current song with repeat-one.
    [[nodiscard]] QueueAdvance advance(QueueDirection direction, AdvanceTrigger trigger,
                                       const QueueRandom& random = systemQueueRandom()) const;
    // Puts `tracks` at the front of the user queue; a song already in it
    // moves to the front instead of appearing twice.
    [[nodiscard]] QueueEngine insertPlayNext(const QList<Track>& tracks) const;
    // Adds `tracks` at the end of the user queue, skipping songs already in it.
    [[nodiscard]] QueueEngine appendUserQueue(const QList<Track>& tracks) const;
    // On: a fresh order anchored on the current song. Off: the shuffled order
    // becomes the context's own order, so nothing played or upcoming moves.
    [[nodiscard]] QueueEngine withShuffle(Shuffle shuffle, const QueueRandom& random = systemQueueRandom()) const;
    [[nodiscard]] QueueEngine withRepeatMode(RepeatMode repeatMode) const;
    // Adds at most MaximumAutoplayTracks songs that are in neither queue to
    // the end of the context (and of the shuffle order, when it is in use).
    [[nodiscard]] QueueEngine appendAutoplayTracks(const QList<Track>& tracks) const;

    // Edits of the visible queue, by visible row (QueueEngineEdits.cpp). An
    // index outside it, or a move past either end, returns the queue unchanged.
    [[nodiscard]] QueueEngine removeVisibleEntry(qsizetype visibleIndex) const;
    // Puts a removed entry back near `visibleIndex`, clamped so a user entry
    // stays in the user queue and a context entry stays after it. Unchanged
    // when the song is already back in either queue.
    [[nodiscard]] QueueEngine restoreVisibleEntry(qsizetype visibleIndex, const QueueEntry& entry) const;
    [[nodiscard]] QueueEngine moveVisibleEntry(qsizetype visibleIndex, QueueDirection delta) const;
    // Rebuilds both queues from a reordered visible list. The first rows (as
    // many as the user queue had) become the user queue, the rest follow the
    // current song. Played songs stay before it unless repeat-all is on or
    // they were moved. A shuffled queue keeps the new order as its play order.
    [[nodiscard]] QueueEngine commitVisibleOrder(const QList<Track>& visibleTracks) const;

    // The queue in play order within `limits`, for saving.
    [[nodiscard]] QueueSnapshot snapshot(const QueueSnapshotLimits& limits) const;

    static constexpr qsizetype MaximumAutoplayTracks = 20;

    friend bool operator==(const QueueEngine&, const QueueEngine&) = default;

private:
    explicit QueueEngine(QueueState state);

    // The context indices in play order: the shuffle order when in use,
    // otherwise 0 .. n-1.
    [[nodiscard]] QList<qsizetype> playOrder() const;

    QueueState m_state;
};

// The result of QueueEngine::advance: the queue after the step, and the song
// to play, which is empty exactly when `ended` is true.
struct QueueAdvance {
    QueueEngine queue;
    std::optional<Track> track;
    bool ended = false;
};

} // namespace lmp::core
