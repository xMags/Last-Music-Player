#include "core/playback/QueueShuffle.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>
#include <utility>

namespace lmp::core {

QueueRandom systemQueueRandom()
{
    return []() { return QRandomGenerator::global()->generateDouble(); };
}

QList<qsizetype> shuffledIndices(qsizetype length, const QueueRandom& random)
{
    QList<qsizetype> indices;
    indices.reserve(std::max<qsizetype>(0, length));
    for (qsizetype index = 0; index < length; ++index) {
        indices.append(index);
    }
    for (qsizetype index = indices.size() - 1; index > 0; --index) {
        // floor(random * (index + 1)), kept inside [0, index] even when the
        // generator breaks its contract (1.0, a negative value or NaN).
        const double scaled = std::floor(random() * static_cast<double>(index + 1));
        const double bounded = std::isnan(scaled) ? 0.0 : std::clamp(scaled, 0.0, static_cast<double>(index));
        const auto swap = static_cast<qsizetype>(bounded);
        std::swap(indices[index], indices[swap]);
    }
    return indices;
}

QList<qsizetype> anchoredShuffleOrder(qsizetype length, qsizetype currentIndex, const QueueRandom& random)
{
    if (length <= 0) {
        return {};
    }
    const qsizetype anchor = currentIndex >= 0 && currentIndex < length ? currentIndex : 0;
    QList<qsizetype> order;
    order.reserve(length);
    order.append(anchor);
    for (const qsizetype index : shuffledIndices(length, random)) {
        if (index != anchor) {
            order.append(index);
        }
    }
    return order;
}

bool isPermutation(const QList<qsizetype>& order, qsizetype length)
{
    if (order.size() != length) {
        return false;
    }
    QList<bool> seen(length, false);
    for (const qsizetype index : order) {
        if (index < 0 || index >= length || seen[index]) {
            return false;
        }
        seen[index] = true;
    }
    return true;
}

} // namespace lmp::core
