#pragma once

#include "core/playback/PlaybackClock.h"
#include "core/playback/QueueTypes.h"

#include <chrono>
#include <memory>

namespace lmp::core {

class SettingsStore;

// The saved playback choices, under the keys earlier versions use:
// ShuffleEnabled (bool, default off), RepeatMode (0, 1 or 2, default 0;
// anything else reads as 0), Autoplay (bool, default on) and Volume (0 to 1).
//
// Volume follows the earlier rules: a stored 0 (or anything at or below
// 0.0001) is silence at launch, so it is replaced by 0.7 and saved back when
// the preferences are read; a change is saved only once the level has been
// left alone for 350 ms, so dragging the slider writes the file once. Mute
// is not a preference and never reaches this class.
//
// Lives on the thread of its owner (the player controller); the clock's
// timer calls back there.
class PlaybackPreferences final {
public:
    static constexpr std::chrono::milliseconds VolumeSaveDelay{350};
    static constexpr double SilentVolumeThreshold = 0.0001;
    static constexpr double LaunchVolumeAfterSilence = 0.7;

    // Reads every value now (and applies the launch volume rule).
    PlaybackPreferences(SettingsStore& settings, PlaybackClock& clock);
    ~PlaybackPreferences();

    PlaybackPreferences(const PlaybackPreferences&) = delete;
    PlaybackPreferences& operator=(const PlaybackPreferences&) = delete;

    [[nodiscard]] Shuffle shuffle() const noexcept { return m_shuffle; }
    [[nodiscard]] RepeatMode repeatMode() const noexcept { return m_repeatMode; }
    [[nodiscard]] bool autoplay() const noexcept { return m_autoplay; }
    // The level as read at launch, or the last one queued for saving.
    [[nodiscard]] double volume() const noexcept { return m_volume; }

    // Saved at once.
    void setShuffle(Shuffle shuffle);
    void setRepeatMode(RepeatMode repeatMode);
    void setAutoplay(bool autoplay);
    // Clamped to 0 to 1 and saved after VolumeSaveDelay without another change.
    void queueVolumeSave(double volume);
    // Saves a queued volume now (for shutdown); nothing when none is queued.
    void flushVolume();

private:
    SettingsStore& m_settings;
    std::unique_ptr<PlaybackTimer> m_volumeTimer;
    Shuffle m_shuffle = Shuffle::Off;
    RepeatMode m_repeatMode = RepeatMode::Off;
    bool m_autoplay = true;
    double m_volume = 0.7;
    bool m_volumeQueued = false;
};

} // namespace lmp::core
