#include "core/playback/PlayerController.h"

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueSnapshot.h"
#include "core/playback/QueueTrackLists.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lmp::core {

PlayerController::PlayerController(PlayerControllerServices services, QueueEngine queue, QueueRandom random,
                                   QObject* parent)
    : QObject(parent)
    , m_localAudio(services.audio)
    , m_localResolver(services.resolver)
    , m_audio(&services.audio)
    , m_resolver(&services.resolver)
    , m_clock(services.clock)
    , m_random(std::move(random))
    , m_preferences(services.settings, services.clock)
    , m_history(services.tracks)
    , m_queue(std::move(queue))
{
    m_brokenSkipTimer = m_clock.createTimer([this]() { skipBrokenTrack(); });

    QueueEngine restored = m_queue.withRepeatMode(m_preferences.repeatMode());
    if (restored.state().shuffle != (m_preferences.shuffle() == Shuffle::On)) {
        restored = restored.withShuffle(m_preferences.shuffle(), m_random);
    }
    m_queue = std::move(restored);
    m_volume = m_preferences.volume();
    applyVolume();

    connectAudio();
}

void PlayerController::connectAudio()
{
    connect(m_audio, &platform::IAudioPlayer::stateChanged, this, &PlayerController::onAudioState);
    connect(m_audio, &platform::IAudioPlayer::positionChanged, this, &PlayerController::onAudioPosition);
    connect(m_audio, &platform::IAudioPlayer::durationChanged, this, &PlayerController::onAudioDuration);
    connect(m_audio, &platform::IAudioPlayer::errorOccurred, this, &PlayerController::onAudioError);
}

void PlayerController::disconnectAudio()
{
    disconnect(m_audio, nullptr, this, nullptr);
}

void PlayerController::setSink(IPlaybackSink* sink, SinkHandover handover)
{
    if (sink == m_sink) {
        return;
    }
    // Whatever was open on the old sink is closed first, unheard: its id is
    // forgotten and its signals no longer reach this controller.
    const bool wasPlaying = m_state == State::Playing || (m_state == State::Loading && m_wantsPlay);
    const bool hadSource = m_audioLoadId != 0 || m_state == State::Loading;
    m_loads.invalidate();
    disconnectAudio();
    if (m_audioLoadId != 0) {
        m_audioLoadId = 0;
        m_audio->stop();
    }
    m_sink = sink;
    m_audio = sink != nullptr ? &sink->audio() : &m_localAudio;
    m_resolver = sink != nullptr ? &sink->resolver() : &m_localResolver;
    connectAudio();
    applyVolume();
    if (!m_current || !hadSource) {
        return;
    }
    if (handover == SinkHandover::Stop) {
        m_wantsPlay = false;
        setState(State::Paused);
        return;
    }
    if (m_sink != nullptr && !m_sink->accepts(*m_current)) {
        const Track refused = *m_current;
        setSink(nullptr, handover);
        emit sinkRefused(refused);
        return;
    }
    reopenCurrent(handover == SinkHandover::KeepPlaying && wasPlaying);
}

PlayerController::~PlayerController() = default;

Shuffle PlayerController::shuffle() const noexcept
{
    return m_queue.state().shuffle ? Shuffle::On : Shuffle::Off;
}

void PlayerController::playInContext(const Track& track, const QList<Track>& context, Shuffle shuffle)
{
    if (track.unavailable) {
        emit trackUnavailable(track);
        return;
    }
    m_backHistory.clear();
    emit contextReplaced();

    const QList<Track> tracks = uniqueQueueTracks(context);
    const QString identity = queueTrackIdentity(track);
    qsizetype selectedIndex = 0;
    for (qsizetype index = 0; index < tracks.size(); ++index) {
        if (queueTrackIdentity(tracks.at(index)) == identity) {
            selectedIndex = index;
            break;
        }
    }
    const Track selected = tracks.isEmpty() ? track : tracks.at(selectedIndex);

    // A new list plays in its own order unless it was started by a shuffle
    // action; inheriting the switch would shuffle every list after one
    // shuffled play.
    const bool shuffleChangedNow = this->shuffle() != shuffle;
    QueueState state = m_queue.state();
    state.shuffle = shuffle == Shuffle::On;
    state.shuffleOrder.clear();
    applyQueue(QueueEngine::fromState(std::move(state))
                   .replaceContext(tracks.isEmpty() ? QList<Track>{selected} : tracks, selectedIndex, m_random));
    if (shuffleChangedNow) {
        m_preferences.setShuffle(shuffle);
        emit shuffleChanged();
    }
    load(selected, 0.0, true, QueueDirection::Next, AdvanceTrigger::Automatic);
    emit autoplayExtensionWanted(false);
}

void PlayerController::playTrack(const Track& track, const QList<Track>& following)
{
    playInContext(track, QList<Track>{track} + following);
}

void PlayerController::playNext(const QList<Track>& tracks)
{
    const QList<Track> incoming = uniqueQueueTracks(tracks);
    if (incoming.isEmpty()) {
        return;
    }
    if (!m_current) {
        playTrack(incoming.first(), incoming.sliced(1));
        return;
    }
    applyQueue(m_queue.insertPlayNext(incoming));
}

void PlayerController::addToQueue(const QList<Track>& tracks)
{
    const QList<Track> incoming = uniqueQueueTracks(tracks);
    if (incoming.isEmpty()) {
        return;
    }
    if (!m_current) {
        playTrack(incoming.first(), incoming.sliced(1));
        return;
    }
    applyQueue(m_queue.appendUserQueue(incoming));
}

void PlayerController::shuffleAndPlay(const QList<Track>& tracks)
{
    const QList<Track> unique = uniqueQueueTracks(tracks);
    if (unique.isEmpty()) {
        return;
    }
    QList<Track> shuffled;
    shuffled.reserve(unique.size());
    for (const qsizetype index : shuffledIndices(unique.size(), m_random)) {
        shuffled.append(unique.at(index));
    }
    playInContext(shuffled.first(), shuffled, Shuffle::On);
}

void PlayerController::playVisibleQueueEntry(qsizetype visibleIndex)
{
    const QList<QueueEntry> entries = m_queue.visibleQueue();
    if (!isQueueIndexInRange(visibleIndex, entries.size())) {
        return;
    }
    const QueueEntry entry = entries.at(visibleIndex);
    const bool fromUserQueue = entry.kind == QueueEntryKind::User;
    m_backHistory.remember(m_current, m_queue, fromUserQueue ? std::optional<Track>(entry.track) : std::nullopt);
    if (fromUserQueue) {
        applyQueue(m_queue.removeVisibleEntry(visibleIndex));
    } else {
        QueueState state = m_queue.state();
        state.contextIndex = entry.sourceIndex;
        applyQueue(QueueEngine::fromState(std::move(state)));
    }
    load(entry.track, 0.0, true, QueueDirection::Next, AdvanceTrigger::Automatic);
    emit autoplayExtensionWanted(false);
}

void PlayerController::appendAutoplayTracks(const QList<Track>& tracks, bool resume)
{
    const QueueEngine extended = m_queue.appendAutoplayTracks(tracks);
    if (extended.upcomingCount() == m_queue.upcomingCount()) {
        return;
    }
    applyQueue(extended);
    // The queue had run out on its last song: go on with the first new one.
    if (resume && m_current && m_state != State::Playing) {
        advance(QueueDirection::Next, AdvanceTrigger::Automatic, StepReason::User);
    }
}

void PlayerController::setAccountPlaybackSink(AccountPlaybackSink* sink)
{
    m_history.setAccountSink(sink);
}

void PlayerController::togglePlay()
{
    if (!m_current) {
        return;
    }
    // Nothing open for the current song (a failed load, a stop, or a song
    // shown without its source): load it again where it stood.
    const bool resolving = m_state == State::Loading && m_audioLoadId == 0;
    if (m_state == State::Error || m_state == State::Idle || (m_audioLoadId == 0 && !resolving)) {
        load(*m_current, m_position, true, m_skipDirection, m_skipTrigger);
        return;
    }
    const bool playingOrAboutTo = m_state == State::Playing || (m_state == State::Loading && m_wantsPlay);
    m_wantsPlay = !playingOrAboutTo;
    if (m_audioLoadId == 0) {
        return; // applied when the source opens
    }
    if (m_wantsPlay) {
        m_audio->play();
    } else {
        m_audio->pause();
    }
}

void PlayerController::next()
{
    advance(QueueDirection::Next, AdvanceTrigger::Manual, StepReason::User);
}

void PlayerController::previous()
{
    if (m_current && m_position > PreviousRestartSeconds) {
        seek(0.0);
        return;
    }
    if (std::optional<PlaybackBackHistory::Restored> restored = m_backHistory.restorePrevious(m_queue)) {
        applyQueue(restored->queue);
        load(restored->track, 0.0, true, QueueDirection::Previous, AdvanceTrigger::Manual);
        emit autoplayExtensionWanted(false);
        return;
    }
    advance(QueueDirection::Previous, AdvanceTrigger::Manual, StepReason::User);
}

void PlayerController::seek(double seconds)
{
    if (!m_current || !std::isfinite(seconds)) {
        return;
    }
    const double limit = m_duration > 0.0 ? m_duration : std::max(0.0, seconds);
    const double target = std::clamp(seconds, 0.0, limit);
    setPosition(target);
    // While resolving there is nothing open yet: the position becomes the
    // start position of the open.
    if (m_audioLoadId != 0 && m_state != State::Error) {
        m_audio->seek(target);
    }
}

void PlayerController::clearUpcoming()
{
    applyQueue(m_queue.commitVisibleOrder({}));
}

void PlayerController::removeVisibleEntry(qsizetype visibleIndex)
{
    applyQueue(m_queue.removeVisibleEntry(visibleIndex));
}

void PlayerController::restoreVisibleEntry(qsizetype visibleIndex, const QueueEntry& entry)
{
    applyQueue(m_queue.restoreVisibleEntry(visibleIndex, entry));
}

void PlayerController::moveVisibleEntry(qsizetype visibleIndex, QueueDirection delta)
{
    applyQueue(m_queue.moveVisibleEntry(visibleIndex, delta));
}

void PlayerController::commitVisibleOrder(const QList<Track>& visibleTracks)
{
    applyQueue(m_queue.commitVisibleOrder(visibleTracks));
}

void PlayerController::restore(const QueueEngine& queue, const Track& track, double startAtSeconds)
{
    if (m_current || !std::isfinite(startAtSeconds)) {
        return;
    }
    applyQueue(queue);
    m_current = track;
    m_skipDirection = QueueDirection::Next;
    m_skipTrigger = AdvanceTrigger::Automatic;
    emit currentTrackChanged();
    setDuration(std::max(0.0, track.durationSeconds));
    setPosition(std::max(0.0, startAtSeconds));
}

void PlayerController::takeOver(const Track& track, const QList<Track>& upcoming, Shuffle shuffle,
                                RepeatMode repeatMode, double startAtSeconds, bool play)
{
    if (track.unavailable || queueTrackIdentity(track).isEmpty()) {
        emit trackUnavailable(track);
        return;
    }
    m_backHistory.clear();
    emit contextReplaced();

    // The current song first, so it is index 0 of the de-duplicated list.
    QueueSnapshot snapshot;
    snapshot.contextQueue = uniqueQueueTracks(QList<Track>{track} + upcoming);
    snapshot.contextIndex = 0;
    const bool shuffleChangedNow = this->shuffle() != shuffle;
    const bool repeatChangedNow = this->repeatMode() != repeatMode;
    applyQueue(QueueEngine::restored(snapshot, shuffle, repeatMode));
    if (shuffleChangedNow) {
        m_preferences.setShuffle(shuffle);
        emit shuffleChanged();
    }
    if (repeatChangedNow) {
        m_preferences.setRepeatMode(repeatMode);
        emit repeatModeChanged();
    }
    load(track, std::isfinite(startAtSeconds) ? std::max(0.0, startAtSeconds) : 0.0, play, QueueDirection::Next,
         AdvanceTrigger::Automatic);
}

void PlayerController::pauseForOtherDevice()
{
    m_brokenSkipTimer->stop();
    m_wantsPlay = false;
    if (m_audioLoadId != 0) {
        m_audio->pause();
    }
}

PlayerController::Removal PlayerController::removeTracks(const std::function<bool(const Track&)>& drop)
{
    Removal removal;
    for (const QueueEntry& entry : m_queue.visibleQueue()) {
        if (drop(entry.track)) {
            ++removal.removedFromQueue;
        }
    }
    if (m_current && drop(*m_current)) {
        removal.stoppedCurrent = true;
        reset();
        return removal;
    }
    m_backHistory.forget(drop);
    applyQueue(m_queue.withoutTracks(drop));
    return removal;
}

void PlayerController::reset()
{
    // A load still on its way must not bring a song back.
    m_loads.invalidate();
    m_audio->stop();
    m_history.clear();
    m_backHistory.clear();
    applyQueue(QueueEngine().withShuffle(shuffle(), m_random).withRepeatMode(repeatMode()));
    emit contextReplaced();
    if (m_current) {
        m_current.reset();
        emit currentTrackChanged();
    }
    setDuration(0.0);
    setPosition(0.0);
    setState(State::Idle);
}

void PlayerController::setShuffle(Shuffle shuffle)
{
    if (this->shuffle() == shuffle) {
        return;
    }
    applyQueue(m_queue.withShuffle(shuffle, m_random));
    m_preferences.setShuffle(shuffle);
    emit shuffleChanged();
}

void PlayerController::setRepeatMode(RepeatMode repeatMode)
{
    if (this->repeatMode() == repeatMode) {
        return;
    }
    applyQueue(m_queue.withRepeatMode(repeatMode));
    m_preferences.setRepeatMode(repeatMode);
    emit repeatModeChanged();
    if (repeatMode == RepeatMode::Off) {
        emit autoplayExtensionWanted(false);
    }
}

void PlayerController::setAutoplay(bool autoplay)
{
    if (m_preferences.autoplay() == autoplay) {
        return;
    }
    m_preferences.setAutoplay(autoplay);
    emit autoplayChanged();
    if (autoplay) {
        emit autoplayExtensionWanted(false);
    }
}

void PlayerController::setVolume(double volume)
{
    if (!std::isfinite(volume)) {
        return;
    }
    const double bounded = std::clamp(volume, 0.0, 1.0);
    const bool volumeMoved = bounded != m_volume;
    const bool wasMuted = m_muted;
    m_volume = bounded;
    m_muted = false; // any change of level unmutes
    applyVolume();
    m_preferences.queueVolumeSave(bounded);
    if (volumeMoved) {
        emit volumeChanged();
    }
    if (wasMuted) {
        emit mutedChanged();
    }
}

void PlayerController::setMuted(bool muted)
{
    if (m_muted == muted) {
        return;
    }
    m_muted = muted;
    applyVolume();
    emit mutedChanged();
}

void PlayerController::shutdown()
{
    m_preferences.flushVolume();
}

void PlayerController::applyVolume()
{
    m_audio->setVolume(m_muted ? 0.0 : m_volume);
}

void PlayerController::applyQueue(const QueueEngine& queue)
{
    if (queue == m_queue) {
        return;
    }
    m_queue = queue;
    emit queueChanged();
}

void PlayerController::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void PlayerController::setPosition(double seconds)
{
    if (m_position == seconds) {
        return;
    }
    m_position = seconds;
    emit positionChanged();
}

void PlayerController::setDuration(double seconds)
{
    if (m_duration == seconds) {
        return;
    }
    m_duration = seconds;
    emit durationChanged();
}

void PlayerController::setStoppedAfterBrokenTracks(bool stopped)
{
    if (m_stoppedAfterBrokenTracks == stopped) {
        return;
    }
    m_stoppedAfterBrokenTracks = stopped;
    emit stoppedAfterBrokenTracksChanged();
}

} // namespace lmp::core
