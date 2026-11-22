#include "core/playback/QueueTrackIdentity.h"

#include <QLatin1StringView>

namespace lmp::core {

QString queueTrackIdentity(const Track& track)
{
    if (track.isRemote()) {
        return track.remoteId.isEmpty() ? QString() : QLatin1StringView("remote|") + track.remoteId;
    }
    return track.filePath.isEmpty() ? QString() : QLatin1StringView("local|") + track.filePath;
}

} // namespace lmp::core
