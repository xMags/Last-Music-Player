#pragma once

#include "core/model/Track.h"

#include <QList>
#include <QtGlobal>

#include <functional>

namespace lmp::core {

// The numeric values are the stored ones: 0 off, 1 the whole queue, 2 one song.
enum class RepeatMode { Off = 0, All = 1, One = 2 };

enum class Shuffle : bool { Off = false, On = true };

// Which way a step through the queue goes; the values are the step itself.
enum class QueueDirection { Previous = -1, Next = 1 };

// A step the user asked for, or one taken because the song ended. Only an
// automatic step stops at the end of the queue or replays a repeat-one song.
enum class AdvanceTrigger { Manual, Automatic };

// A source of numbers in [0, 1). Injected so tests can make shuffling
// predictable; systemQueueRandom() in QueueShuffle.h is the default.
using QueueRandom = std::function<double()>;

// The raw state of a queue. The user queue ("play next", "add to queue")
// always plays before the context, the list the song was started from.
// shuffleOrder, when it is as long as the context, lists context indices in
// the order they play; any other length means it is not in use.
struct QueueState {
    QList<Track> contextQueue;
    // The current song in the context, or -1 when nothing in it is current.
    qsizetype contextIndex = -1;
    QList<Track> userQueue;
    bool shuffle = false;
    QList<qsizetype> shuffleOrder;
    RepeatMode repeatMode = RepeatMode::Off;

    friend bool operator==(const QueueState&, const QueueState&) = default;
};

enum class QueueEntryKind { User, Context };

// One row of the visible queue. sourceIndex is the row's index in the user
// queue or in the context, depending on kind.
struct QueueEntry {
    Track track;
    QueueEntryKind kind = QueueEntryKind::Context;
    qsizetype sourceIndex = 0;

    friend bool operator==(const QueueEntry&, const QueueEntry&) = default;
};

} // namespace lmp::core
