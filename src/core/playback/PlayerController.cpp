#include "core/playback/PlayerController.h"

#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTrackLists.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lmp::core {

PlayerController::PlayerController(PlayerControllerServices services, QueueEngine queue, QueueRandom random,
                                   QObject* parent)
    : QObject(parent)
    , m_audio(services.audio)
    , m_resolver(services.resolver)
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

    connect(&m_audio, &platform::IAudioPlayer::stateChanged, this, &PlayerController::onAudioState);
    connect(&m_audio, &platform::IAudioPlayer::positionChanged, this, &PlayerController::onAudioPosition);
    connect(&m_audio, &platform::IAudioPlayer::durationChanged, this, &PlayerController::onAudioDuration);
    connect(&m_audio, &platform::IAudioPlayer::errorOccurred, this, &PlayerController::onAudioError);
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
        m_audio.play();
    } else {
        m_audio.pause();
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
        m_audio.seek(target);
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
    m_audio.setVolume(m_muted ? 0.0 : m_volume);
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
