#pragma once

#include "core/playback/QueueTypes.h"

#include <QList>
#include <QtGlobal>

namespace lmp::core {

// Numbers from the process-wide Qt generator. Safe to call from any thread.
[[nodiscard]] QueueRandom systemQueueRandom();

// 0 .. length-1 in a Fisher-Yates order drawn from `random`. A value outside
// [0, 1) from the generator is clamped, so the result is always a permutation.
[[nodiscard]] QList<qsizetype> shuffledIndices(qsizetype length, const QueueRandom& random);

// A shuffled play order that starts with the current song, so turning shuffle
// on never changes what is playing. An index outside the context anchors on
// the first song. Empty for an empty context.
[[nodiscard]] QList<qsizetype> anchoredShuffleOrder(qsizetype length, qsizetype currentIndex,
                                                    const QueueRandom& random);

// True when `order` holds every index 0 .. length-1 exactly once.
[[nodiscard]] bool isPermutation(const QList<qsizetype>& order, qsizetype length);

} // namespace lmp::core
