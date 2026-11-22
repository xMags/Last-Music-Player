#include "core/playback/PlaybackPreferences.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <algorithm>

namespace lmp::core {

namespace {

[[nodiscard]] RepeatMode repeatModeFromStored(int stored)
{
    switch (stored) {
    case 1:
        return RepeatMode::All;
    case 2:
        return RepeatMode::One;
    default:
        return RepeatMode::Off;
    }
}

} // namespace

PlaybackPreferences::PlaybackPreferences(SettingsStore& settings, PlaybackClock& clock)
    : m_settings(settings)
{
    m_volumeTimer = clock.createTimer([this]() { flushVolume(); });

    m_shuffle = m_settings.boolean(settings::keys::ShuffleEnabled, false) ? Shuffle::On : Shuffle::Off;
    m_repeatMode = repeatModeFromStored(m_settings.integer(settings::keys::RepeatMode, 0));
    m_autoplay = m_settings.boolean(settings::keys::Autoplay, true);

    m_volume = m_settings.volume();
    // A saved 0 would start the app silent (earlier versions could save it
    // while muted), so launch at an audible level and keep that one.
    if (m_volume <= SilentVolumeThreshold) {
        m_volume = LaunchVolumeAfterSilence;
        m_settings.setVolume(m_volume);
    }
}

PlaybackPreferences::~PlaybackPreferences() = default;

void PlaybackPreferences::setShuffle(Shuffle shuffle)
{
    m_shuffle = shuffle;
    m_settings.setBoolean(settings::keys::ShuffleEnabled, shuffle == Shuffle::On);
}

void PlaybackPreferences::setRepeatMode(RepeatMode repeatMode)
{
    m_repeatMode = repeatMode;
    m_settings.setInteger(settings::keys::RepeatMode, static_cast<int>(repeatMode));
}

void PlaybackPreferences::setAutoplay(bool autoplay)
{
    m_autoplay = autoplay;
    m_settings.setBoolean(settings::keys::Autoplay, autoplay);
}

void PlaybackPreferences::queueVolumeSave(double volume)
{
    m_volume = std::clamp(volume, 0.0, 1.0);
    m_volumeQueued = true;
    m_volumeTimer->start(VolumeSaveDelay);
}

void PlaybackPreferences::flushVolume()
{
    m_volumeTimer->stop();
    if (!m_volumeQueued) {
        return;
    }
    m_volumeQueued = false;
    m_settings.setVolume(m_volume);
}

} // namespace lmp::core
