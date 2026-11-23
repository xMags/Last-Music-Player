#pragma once

namespace lmp::platform {
class IAudioPlayer;
} // namespace lmp::platform

namespace lmp::core {

class TrackSourceResolver;
struct Track;

// Somewhere other than this PC's own audio output that the player can send
// playback to (a cast device): the player that plays there and the resolver
// that finds what it is given. Transport commands (play, pause, seek,
// volume) go to the active sink's player.
//
// A sink may refuse a song (`accepts` false), for example a local file a
// device cannot reach; the controller then falls back to this PC before
// loading it.
class IPlaybackSink {
public:
    virtual ~IPlaybackSink() = default;

    [[nodiscard]] virtual platform::IAudioPlayer& audio() = 0;
    [[nodiscard]] virtual TrackSourceResolver& resolver() = 0;
    [[nodiscard]] virtual bool accepts(const Track& track) const = 0;

protected:
    IPlaybackSink() = default;
    IPlaybackSink(const IPlaybackSink&) = default;
    IPlaybackSink& operator=(const IPlaybackSink&) = default;
};

} // namespace lmp::core
