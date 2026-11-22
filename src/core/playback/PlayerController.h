#pragma once

#include "core/async/RequestScope.h"
#include "core/async/Result.h"
#include "core/model/Track.h"
#include "core/playback/BrokenTrackGuard.h"
#include "core/playback/PlaybackBackHistory.h"
#include "core/playback/PlaybackHistoryRecorder.h"
#include "core/playback/PlaybackPreferences.h"
#include "core/playback/PlayerControllerServices.h"
#include "core/playback/QueueEngine.h"
#include "core/playback/QueueShuffle.h"
#include "platform/IAudioPlayer.h"

#include <QList>
#include <QObject>
#include <QUrl>

#include <chrono>
#include <memory>
#include <optional>

namespace lmp::core {

// Plays the queue: starts songs through the resolver and the audio player,
// moves through the queue (QueueEngine rules), skips broken songs, keeps the
// playback preferences and writes plays to the history. Volume is 0 to 1.
//
// Lives on the GUI thread with the audio player, whose signals it handles
// directly. Resolver answers arrive on this thread through continuations
// bound to this object and are dropped once a newer load started. Every
// signal is emitted on this thread; read the new value with the getter.
class PlayerController final : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Loading, Paused, Playing, Error };
    Q_ENUM(State)

    // Previous restarts the song instead once it is further in than this.
    static constexpr double PreviousRestartSeconds = 3.0;
    // How long a failed song stays shown before the next one is tried.
    static constexpr std::chrono::milliseconds BrokenTrackSkipDelay{250};

    // `queue` is the starting queue; the saved shuffle and repeat replace its own.
    PlayerController(PlayerControllerServices services, QueueEngine queue,
                     QueueRandom random = systemQueueRandom(), QObject* parent = nullptr);
    ~PlayerController() override;

    [[nodiscard]] const std::optional<Track>& currentTrack() const noexcept { return m_current; }
    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] double positionSeconds() const noexcept { return m_position; }
    [[nodiscard]] double durationSeconds() const noexcept { return m_duration; }
    [[nodiscard]] double volume() const noexcept { return m_volume; } // kept while muted
    [[nodiscard]] bool muted() const noexcept { return m_muted; }
    [[nodiscard]] Shuffle shuffle() const noexcept;
    [[nodiscard]] RepeatMode repeatMode() const noexcept { return m_queue.state().repeatMode; }
    [[nodiscard]] bool autoplay() const noexcept { return m_preferences.autoplay(); }
    [[nodiscard]] const QueueEngine& queue() const noexcept { return m_queue; }
    [[nodiscard]] QList<QueueEntry> visibleQueue() const { return m_queue.visibleQueue(); }
    // Three songs in a row failed and playback stopped; cleared by the next load.
    [[nodiscard]] bool stoppedAfterBrokenTracks() const noexcept { return m_stoppedAfterBrokenTracks; }

    // Starts `track` with `context` as the list to go on with, in list order
    // unless `shuffle` is on (the preference follows). Clears the way back.
    void playInContext(const Track& track, const QList<Track>& context, Shuffle shuffle = Shuffle::Off);
    void playTrack(const Track& track, const QList<Track>& following = {});
    // Front or end of the user queue; with nothing playing they start playback.
    void playNext(const QList<Track>& tracks);
    void addToQueue(const QList<Track>& tracks);
    void shuffleAndPlay(const QList<Track>& tracks);
    void playVisibleQueueEntry(qsizetype visibleIndex);
    void togglePlay();
    void next();
    // Restarts the song past PreviousRestartSeconds, otherwise goes back
    // through the songs played before, then through the queue.
    void previous();
    void seek(double seconds);
    // Empties the visible queue; the current song plays on.
    void clearUpcoming();
    // Shows `track` as the current song with `queue` as the queue, without
    // opening anything (the state stays Idle); the first togglePlay() opens
    // it at `startAtSeconds`. For the launch restore: ignored while a song
    // is current, and nothing is emitted for a song left.
    void restore(const QueueEngine& queue, const Track& track, double startAtSeconds);

    void setShuffle(Shuffle shuffle);
    void setRepeatMode(RepeatMode repeatMode);
    void setAutoplay(bool autoplay);
    // Clamped to 0 to 1; unmutes; saved once changes pause.
    void setVolume(double volume);
    void setMuted(bool muted); // never saved
    // Saves what is still pending (the volume); called by the composition root.
    void shutdown();

signals:
    void currentTrackChanged();
    void stateChanged();
    void positionChanged();
    void durationChanged();
    void volumeChanged();
    void mutedChanged();
    void shuffleChanged();
    void repeatModeChanged();
    void autoplayChanged();
    void queueChanged();
    void stoppedAfterBrokenTracksChanged();
    void trackFailed(const lmp::core::Track& track);      // no source, or the player failed
    void trackUnavailable(const lmp::core::Track& track); // asked for; nothing changed
    void playRecorded(const lmp::core::Track& track);     // the play reached the history
    void trackEnded(const lmp::core::Track& track);       // played to its end
    // Replaced before its end (for saving where it stopped).
    void trackLeft(const lmp::core::Track& track, double positionSeconds, double durationSeconds);
    // A point where related songs may top up the queue (`queueEnded`: it ran
    // out). Nothing listens yet; the autoplay service decides whether to act.
    void autoplayExtensionWanted(bool queueEnded);

private:
    enum class StepReason { User, TrackEnded, BrokenTrack };

    void load(const Track& track, double startAtSeconds, bool autoplay, QueueDirection skipDirection,
              AdvanceTrigger skipTrigger);
    void advance(QueueDirection direction, AdvanceTrigger trigger, StepReason reason);
    void replayCurrent();
    void applyQueue(const QueueEngine& queue);
    void handleResolved(RequestScope::Token token, const Result<QUrl>& source);
    void handleTrackFailure();
    void handleEnded();
    void skipBrokenTrack();
    void onAudioState(quint64 loadId, platform::IAudioPlayer::State state);
    void onAudioPosition(quint64 loadId, double seconds);
    void onAudioDuration(quint64 loadId, double seconds);
    void onAudioError(quint64 loadId);
    void observeHistory(bool playing);
    void applyVolume();
    void setState(State state);
    void setPosition(double seconds);
    void setDuration(double seconds);
    void setStoppedAfterBrokenTracks(bool stopped);

    platform::IAudioPlayer& m_audio;
    TrackSourceResolver& m_resolver;
    PlaybackClock& m_clock;
    QueueRandom m_random;
    PlaybackPreferences m_preferences;
    PlaybackHistoryRecorder m_history;
    PlaybackBackHistory m_backHistory;
    BrokenTrackGuard m_brokenTracks;
    std::unique_ptr<PlaybackTimer> m_brokenSkipTimer;
    RequestScope m_loads;
    QueueEngine m_queue;

    std::optional<Track> m_current;
    quint64 m_audioLoadId = 0; // 0 while resolving or with nothing open
    State m_state = State::Idle;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_volume = PlaybackPreferences::LaunchVolumeAfterSilence;
    bool m_muted = false;
    bool m_wantsPlay = false;
    bool m_startedThisLoad = false;
    bool m_endedThisLoad = false;
    bool m_stoppedAfterBrokenTracks = false;
    // How the current song was reached, so a broken one is skipped the same way.
    QueueDirection m_skipDirection = QueueDirection::Next;
    AdvanceTrigger m_skipTrigger = AdvanceTrigger::Automatic;
};

} // namespace lmp::core
