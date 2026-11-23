// The loading half of PlayerController: starting songs, the audio player's
// events, the steps through the queue, broken songs and the history.

#include "core/playback/PlayerController.h"

#include "core/playback/PlaybackSourceRouter.h"
#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/TrackSourceResolver.h"

#include <QFuture>
#include <QString>

#include <algorithm>
#include <utility>

namespace lmp::core {

void PlayerController::load(const Track& track, double startAtSeconds, bool autoplay, QueueDirection skipDirection,
                            AdvanceTrigger skipTrigger)
{
    if (m_current && !m_endedThisLoad) {
        emit trackLeft(*m_current, m_position, m_duration);
    }
    m_brokenSkipTimer->stop();
    setStoppedAfterBrokenTracks(false);

    // Silence the previous song while the new one resolves. Its id is
    // forgotten first, so the Idle it reports is ignored.
    const bool somethingOpen = m_audioLoadId != 0;
    m_audioLoadId = 0;
    if (somethingOpen) {
        m_audio.stop();
    }

    const RequestScope::Token token = m_loads.next();
    m_current = track;
    m_wantsPlay = autoplay;
    m_startedThisLoad = false;
    m_endedThisLoad = false;
    m_skipDirection = skipDirection;
    m_skipTrigger = skipTrigger;
    m_history.begin(track);
    setState(State::Loading);
    emit currentTrackChanged();
    setPosition(std::max(0.0, startAtSeconds));
    setDuration(std::max(0.0, track.durationSeconds));

    // May run at once when the answer is ready; everything above is in
    // place by then. A cancelled resolve counts as a failed one.
    m_resolver.resolve(track)
        .then(this, [this, token](Result<QUrl> source) { handleResolved(token, source); })
        .onCanceled(this, [this, token]() {
            handleResolved(token, Failure::of(FailureKind::Cancelled, QStringLiteral("The resolve was cancelled.")));
        });
}

void PlayerController::handleResolved(RequestScope::Token token, const Result<QUrl>& source)
{
    if (!m_loads.isCurrent(token)) {
        return; // a newer load started
    }
    if (!source) {
        if (PlaybackSourceRouter::isRemote(*m_current)) {
            noticeResolveFailure(source.failure());
        }
        handleTrackFailure();
        return;
    }
    m_audioLoadId = m_audio.open(source.value(), m_wantsPlay, m_position);
    // Re-applied for every song, as before, in case the player reset it.
    applyVolume();
}

void PlayerController::advance(QueueDirection direction, AdvanceTrigger trigger, StepReason reason)
{
    const bool automatic = trigger == AdvanceTrigger::Automatic;
    const RepeatMode repeatMode = m_queue.state().repeatMode;
    // Repeat-one loops the song that is playing, wherever it came from; a
    // broken song is never looped.
    if (automatic && reason != StepReason::BrokenTrack && repeatMode == RepeatMode::One && m_current) {
        replayCurrent();
        return;
    }

    const bool bypassRepeatOne = reason == StepReason::BrokenTrack && repeatMode == RepeatMode::One;
    const QueueEngine from = bypassRepeatOne ? m_queue.withRepeatMode(RepeatMode::Off) : m_queue;
    QueueAdvance result = from.advance(direction, trigger, m_random);
    if (bypassRepeatOne) {
        result.queue = result.queue.withRepeatMode(RepeatMode::One);
    }

    if (direction == QueueDirection::Next && result.track) {
        const QList<Track>& userQueue = m_queue.state().userQueue;
        const bool fromUserQueue =
            !userQueue.isEmpty() && queueTrackIdentity(userQueue.first()) == queueTrackIdentity(*result.track);
        m_backHistory.remember(m_current, m_queue, fromUserQueue ? result.track : std::nullopt);
    }
    applyQueue(result.queue);

    if (result.track) {
        load(*result.track, 0.0, true, direction, trigger);
        emit autoplayExtensionWanted(false);
        return;
    }
    if (!result.ended) {
        return;
    }
    // The queue ran out: the last song stays shown, stopped where it ended.
    m_wantsPlay = false;
    observeHistory(false);
    if (m_audioLoadId != 0 && m_audio.state() == platform::IAudioPlayer::State::Playing) {
        m_audio.pause();
    }
    if (m_state != State::Error) {
        setState(m_current ? State::Paused : State::Idle);
    }
    if (automatic && m_preferences.autoplay() && repeatMode == RepeatMode::Off) {
        emit autoplayExtensionWanted(true);
    }
}

void PlayerController::replayCurrent()
{
    if (m_audioLoadId == 0 || m_state == State::Error) {
        load(*m_current, 0.0, true, m_skipDirection, m_skipTrigger);
        return;
    }
    // Each round is a play of its own in the history.
    m_history.begin(*m_current);
    m_wantsPlay = true;
    m_endedThisLoad = false;
    setPosition(0.0);
    m_audio.play(); // after the end, play starts from the beginning
}

void PlayerController::handleTrackFailure()
{
    m_wantsPlay = false;
    observeHistory(false);
    setState(State::Error);
    emit trackFailed(*m_current);

    if (m_queue.visibleQueue().isEmpty()) {
        return; // nothing to skip to
    }
    if (!m_brokenTracks.skipAfterFailure()) {
        setStoppedAfterBrokenTracks(true);
        if (PlaybackSourceRouter::isRemote(*m_current)) {
            emit playbackNotice(PlaybackNotice::RemoteFailuresStopped, *m_current);
        }
        return;
    }
    // Any newer load stops the timer, so when it fires the failed song is
    // still the current one.
    m_brokenSkipTimer->start(BrokenTrackSkipDelay);
}

void PlayerController::skipBrokenTrack()
{
    if (!m_current || m_state != State::Error) {
        return;
    }
    advance(m_skipDirection, m_skipTrigger, StepReason::BrokenTrack);
}

void PlayerController::handleEnded()
{
    m_endedThisLoad = true;
    m_wantsPlay = false;
    observeHistory(false);
    if (m_duration > 0.0) {
        setPosition(m_duration);
    }
    emit trackEnded(*m_current);
    advance(QueueDirection::Next, AdvanceTrigger::Automatic, StepReason::TrackEnded);
}

void PlayerController::onAudioState(quint64 loadId, platform::IAudioPlayer::State state)
{
    if (loadId == 0 || loadId != m_audioLoadId || !m_current) {
        return;
    }
    using AudioState = platform::IAudioPlayer::State;
    switch (state) {
    case AudioState::Loading:
        setState(State::Loading);
        return;
    case AudioState::Paused:
        // The pause after opening means "ready"; with play asked for, the
        // song is still starting.
        if (m_wantsPlay && !m_startedThisLoad) {
            return;
        }
        observeHistory(false);
        setState(State::Paused);
        return;
    case AudioState::Playing:
        m_startedThisLoad = true;
        m_wantsPlay = true;
        m_brokenTracks.trackStarted();
        setState(State::Playing);
        observeHistory(true);
        return;
    case AudioState::Ended:
        handleEnded();
        return;
    case AudioState::Error:
        // errorOccurred came first and handled it.
        setState(State::Error);
        return;
    case AudioState::Idle:
        observeHistory(false);
        setState(State::Idle);
        return;
    }
}

void PlayerController::onAudioPosition(quint64 loadId, double seconds)
{
    if (loadId == 0 || loadId != m_audioLoadId) {
        return;
    }
    setPosition(std::max(0.0, seconds));
    if (m_state == State::Playing) {
        observeHistory(true);
    }
}

void PlayerController::onAudioDuration(quint64 loadId, double seconds)
{
    if (loadId == 0 || loadId != m_audioLoadId) {
        return;
    }
    setDuration(std::max(0.0, seconds));
}

void PlayerController::onAudioError(quint64 loadId)
{
    if (loadId == 0 || loadId != m_audioLoadId || !m_current || m_state == State::Error) {
        return;
    }
    // A remote stream can stop working mid-song (its address expired, the
    // connection dropped): it is fetched again rather than skipped.
    if (PlaybackSourceRouter::isRemote(*m_current)) {
        recoverStream();
        return;
    }
    handleTrackFailure();
}

void PlayerController::observeHistory(bool playing)
{
    if (m_history.observe(playing, m_clock.nowMilliseconds()) && m_current) {
        emit playRecorded(*m_current);
    }
}

} // namespace lmp::core
