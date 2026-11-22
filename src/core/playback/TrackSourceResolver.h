#pragma once

#include "core/async/Result.h"
#include "core/model/Track.h"

#include <QFuture>
#include <QUrl>

namespace lmp::core {

// Finds the address the audio player opens for a song: a local file, or a
// remote address a service hands out. The player controller asks once per
// load and drops an answer that arrives after a newer load started, so an
// implementation never has to cancel anything itself.
//
// Called on the controller's thread. The future may finish on any thread;
// the controller takes the result on its own thread.
class TrackSourceResolver {
public:
    virtual ~TrackSourceResolver() = default;

    [[nodiscard]] virtual QFuture<Result<QUrl>> resolve(const Track& track) = 0;

protected:
    TrackSourceResolver() = default;
    TrackSourceResolver(const TrackSourceResolver&) = default;
    TrackSourceResolver& operator=(const TrackSourceResolver&) = default;
};

// Songs on this machine: the file path as a file address. Fails with
// FailureKind::NotFound when the song has no path or the file is gone. The
// answer is ready at once (the check is a single file lookup).
class LocalFileSourceResolver final : public TrackSourceResolver {
public:
    [[nodiscard]] QFuture<Result<QUrl>> resolve(const Track& track) override;
};

} // namespace lmp::core
