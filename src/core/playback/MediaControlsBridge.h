#pragma once

#include <QObject>
#include <QString>

#include "core/model/Track.h"
#include "core/playback/MediaControlsThrottle.h"
#include "core/playback/PlaybackClock.h"
#include "core/playback/PlayerController.h"
#include "core/settings/SettingsStore.h"
#include "platform/IMediaControls.h"

namespace lmp::core {

// Keeps the system's media controls in step with the player: the current
// song's title, artist, album and cover, the play state, and the progress
// (MediaControlsThrottle decides how often), and sends the media keys and
// the system display's buttons back to the player. Follows the
// WindowsMediaControls setting, also when it changes while running.
//
// Lives on the GUI thread with the player and the controls; settings changes
// made on another thread arrive through a queued connection.
class MediaControlsBridge final : public QObject {
    Q_OBJECT

public:
    struct Services {
        PlayerController& player;
        platform::IMediaControls& controls;
        SettingsStore& settings;
        const PlaybackClock& clock;
    };

    explicit MediaControlsBridge(Services services, QObject* parent = nullptr);

    // The play state shown for a player state, with or without a song.
    [[nodiscard]] static platform::IMediaControls::PlaybackStatus statusFor(PlayerController::State state,
                                                                            bool hasTrack) noexcept;
    // The local file the cover of `track` is taken from; empty for none.
    [[nodiscard]] static QString artworkFileFor(const Track& track);

private:
    void applyEnabled();
    void onTrackChanged();
    void onStateChanged();
    void report(bool force);
    void onCommand(platform::IMediaControls::MediaCommand command);

    PlayerController& m_player;
    platform::IMediaControls& m_controls;
    SettingsStore& m_settings;
    MediaControlsThrottle m_throttle;
    platform::IMediaControls::PlaybackStatus m_status = platform::IMediaControls::PlaybackStatus::Closed;
};

} // namespace lmp::core
