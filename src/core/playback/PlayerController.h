#pragma once

#include "core/async/RequestScope.h"
#include "core/async/Result.h"
#include "core/model/Track.h"
#include "core/playback/BrokenTrackGuard.h"
#include "core/playback/IPlaybackSink.h"
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

    // Something about a remote song the user should be told.
    enum class PlaybackNotice {
        // A remote song was asked for while only songs on this PC can play.
        NeedsMusicSource,
        // No stream could be found for the song.
        NoPlayableSource,
        // Several songs in a row could not be played; playback stopped.
        RemoteFailuresStopped,
    };
    Q_ENUM(PlaybackNotice)

    // Previous restarts the song instead once it is further in than this.
    static constexpr double PreviousRestartSeconds = 3.0;
    // How long a failed song stays shown before the next one is tried.
    static constexpr std::chrono::milliseconds BrokenTrackSkipDelay{250};
    // A remote stream that fails is fetched again and reopened where it
    // stopped, at most this many times within StreamRecoveryWindow; past that
    // playback pauses on the song.
    static constexpr int StreamRecoveryAttempts = 3;
    static constexpr qint64 StreamRecoveryWindowMs = 20000;

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
    // Autoplay's songs at the end of the queue (at most 20, none already
    // queued); `resume` goes on to the first of them when the queue had run
    // out and the last song stopped.
    void appendAutoplayTracks(const QList<Track>& tracks, bool resume);
    void playVisibleQueueEntry(qsizetype visibleIndex);
    void togglePlay();
    void next();
    // Restarts the song past PreviousRestartSeconds, otherwise goes back
    // through the songs played before, then through the queue.
    void previous();
    void seek(double seconds);
    // Empties the visible queue; the current song plays on.
    void clearUpcoming();
    // Edits of the visible queue, by visible row (the QueueEngine edits of
    // the same names); the current song plays on. Out-of-range rows change
    // nothing, and queueChanged is emitted only for a real change.
    void removeVisibleEntry(qsizetype visibleIndex);
    void restoreVisibleEntry(qsizetype visibleIndex, const QueueEntry& entry);
    void moveVisibleEntry(qsizetype visibleIndex, QueueDirection delta);
    void commitVisibleOrder(const QList<Track>& visibleTracks);
    // Shows `track` as the current song with `queue` as the queue, without
    // opening anything (the state stays Idle); the first togglePlay() opens
    // it at `startAtSeconds`. For the launch restore: ignored while a song
    // is current, and nothing is emitted for a song left.
    void restore(const QueueEngine& queue, const Track& track, double startAtSeconds);
    // Forgets everything ("Delete all data"): stops the song, empties the
    // queue and the way back, and shows no song. Shuffle, repeat and the
    // volume stay as they are.
    void reset();

    void setShuffle(Shuffle shuffle);
    void setRepeatMode(RepeatMode repeatMode);
    void setAutoplay(bool autoplay);
    // Clamped to 0 to 1; unmutes; saved once changes pause.
    void setVolume(double volume);
    void setMuted(bool muted); // never saved
    // Saves what is still pending (the volume); called by the composition root.
    void shutdown();

    // What happens to the song that plays when the sink changes.
    // Stop leaves it closed and paused where it stood (the next Play opens
    // it on the new sink).
    enum class SinkHandover { KeepPlaying, Pause, Stop };
    // Sends playback to `sink` (a cast device), or back to this PC for
    // nullptr. The current song is opened again on the new sink where it
    // stood; it keeps playing or pauses as `handover` says. The sink must
    // outlive its use here (until the next setSink).
    void setSink(IPlaybackSink* sink, SinkHandover handover);
    [[nodiscard]] bool usesExternalSink() const noexcept { return m_sink != nullptr; }

    // Where plays of account songs go in Account mode instead of the local
    // history (nothing by default). Must outlive the controller.
    void setAccountPlaybackSink(AccountPlaybackSink* sink);

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
    void playbackNotice(lmp::core::PlayerController::PlaybackNotice notice, const lmp::core::Track& track);
    void playRecorded(const lmp::core::Track& track);     // the play reached the history
    void trackEnded(const lmp::core::Track& track);       // played to its end
    // Replaced before its end (for saving where it stopped).
    void trackLeft(const lmp::core::Track& track, double positionSeconds, double durationSeconds);
    // A point where related songs may top up the queue (`queueEnded`: it ran
    // out). The autoplay service decides whether to act.
    void autoplayExtensionWanted(bool queueEnded);
    // A new queue replaced the old one (a new list, or everything forgotten).
    void contextReplaced();
    // The external sink refused the next song, so playback came back to this
    // PC before it was loaded (the sink is no longer used).
    void sinkRefused(const lmp::core::Track& track);

private:
    enum class StepReason { User, TrackEnded, BrokenTrack };

    void load(const Track& track, double startAtSeconds, bool autoplay, QueueDirection skipDirection,
              AdvanceTrigger skipTrigger);
    void advance(QueueDirection direction, AdvanceTrigger trigger, StepReason reason);
    void replayCurrent();
    // Resolves and opens the current song again where it stood, without
    // treating it as a new play.
    void reopenCurrent(bool play);
    void applyQueue(const QueueEngine& queue);
    void handleResolved(RequestScope::Token token, const Result<QUrl>& source);
    void handleTrackFailure();
    void handleEnded();
    void skipBrokenTrack();
    void onAudioState(quint64 loadId, platform::IAudioPlayer::State state);
    void onAudioPosition(quint64 loadId, double seconds);
    void onAudioDuration(quint64 loadId, double seconds);
    void onAudioError(quint64 loadId);
    // Remote streams (PlayerControllerRecovery.cpp).
    void noticeResolveFailure(const Failure& failure);
    void recoverStream();
    void handleRecovered(RequestScope::Token token, const Result<QUrl>& source, double resumeAt);
    void pauseOnFailedStream();
    void observeHistory(bool playing);
    void applyVolume();
    void setState(State state);
    void setPosition(double seconds);
    void setDuration(double seconds);
    void setStoppedAfterBrokenTracks(bool stopped);

    void connectAudio();
    void disconnectAudio();

    // This PC's own player and resolver, and the ones in use (the external
    // sink's while one is set).
    platform::IAudioPlayer& m_localAudio;
    TrackSourceResolver& m_localResolver;
    IPlaybackSink* m_sink = nullptr;
    platform::IAudioPlayer* m_audio = nullptr;
    TrackSourceResolver* m_resolver = nullptr;
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
    // Stream recovery attempts in the current window, and when the last began.
    int m_streamRecoveries = 0;
    std::optional<qint64> m_lastStreamRecoveryMs;
    // How the current song was reached, so a broken one is skipped the same way.
    QueueDirection m_skipDirection = QueueDirection::Next;
    AdvanceTrigger m_skipTrigger = AdvanceTrigger::Automatic;
};

} // namespace lmp::core
