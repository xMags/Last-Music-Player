#pragma once

namespace lmp::platform {
class IAudioPlayer;
}

namespace lmp::core {

class PlaybackClock;
class SettingsStore;
class TrackSourceResolver;
namespace db {
class TrackRepository;
}

// What a PlayerController works with. Every one must outlive the controller
// and belong to its thread (the repository is used for one small write per
// play).
struct PlayerControllerServices {
    platform::IAudioPlayer& audio;
    SettingsStore& settings;
    db::TrackRepository& tracks;
    TrackSourceResolver& resolver;
    PlaybackClock& clock;
};

} // namespace lmp::core
