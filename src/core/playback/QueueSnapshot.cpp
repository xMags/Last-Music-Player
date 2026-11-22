#include "core/playback/QueueSnapshot.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLatin1StringView>

#include <algorithm>
#include <cmath>
#include <limits>

namespace lmp::core {

namespace {

constexpr QLatin1StringView kContextQueue{"contextQueue"};
constexpr QLatin1StringView kContextIndex{"contextIndex"};
constexpr QLatin1StringView kUserQueue{"userQueue"};

QJsonArray tracksToJson(const QList<Track>& tracks)
{
    QJsonArray array;
    for (const Track& track : tracks) {
        array.append(track.toSnapshotJson());
    }
    return array;
}

QList<Track> tracksFromJson(const QJsonValue& value)
{
    QList<Track> tracks;
    if (!value.isArray()) {
        return tracks;
    }
    const QJsonArray array = value.toArray();
    tracks.reserve(array.size());
    for (const QJsonValue& entry : array) {
        if (entry.isObject()) {
            tracks.append(Track::fromSnapshotJson(entry.toObject()));
        }
    }
    return tracks;
}

qsizetype indexFromJson(const QJsonValue& value)
{
    if (!value.isDouble()) {
        return -1;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::trunc(number) != number) {
        return -1;
    }
    // A huge index still means "past the end" (or "before the start"); the
    // restore clamps it into the context either way.
    const double bounded = std::clamp(number, static_cast<double>(std::numeric_limits<int>::min()),
                                      static_cast<double>(std::numeric_limits<int>::max()));
    return static_cast<qsizetype>(bounded);
}

} // namespace

QJsonObject QueueSnapshot::toJson() const
{
    QJsonObject object;
    object.insert(kContextQueue, tracksToJson(contextQueue));
    object.insert(kContextIndex, static_cast<double>(contextIndex));
    object.insert(kUserQueue, tracksToJson(userQueue));
    return object;
}

QueueSnapshot QueueSnapshot::fromJson(const QJsonObject& object)
{
    QueueSnapshot snapshot;
    snapshot.contextQueue = tracksFromJson(object.value(kContextQueue));
    snapshot.contextIndex = indexFromJson(object.value(kContextIndex));
    snapshot.userQueue = tracksFromJson(object.value(kUserQueue));
    return snapshot;
}

} // namespace lmp::core
