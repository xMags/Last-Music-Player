#include "core/playback/PlaybackStatePersistence.h"

#include "core/app/AppPaths.h"
#include "core/db/LibraryQueries.h"
#include "core/playback/LastTrackIdentity.h"
#include "core/playback/PlaybackStateRestore.h"
#include "core/playback/PlayerController.h"
#include "core/playback/QueueShuffle.h"

#include <QFuture>
#include <QList>
#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentRun>

#include <utility>

namespace lmp::core {

namespace {

Q_LOGGING_CATEGORY(lcPersistence, "lmp.playback.persistence")

} // namespace

PlaybackStatePersistence::PlaybackStatePersistence(PlaybackStatePersistenceServices services, const AppPaths& paths,
                                                   QObject* parent)
    : QObject(parent)
    , m_services(services)
    , m_file(paths.stateFile(), services.userDataGate)
    , m_resume(services.tracks)
{
    // The timer is a member, so its callback never outlives this object.
    m_saveTimer = m_services.clock.createTimer([this]() { saveNow(); });

    PlayerController& player = m_services.player;
    connect(&player, &PlayerController::currentTrackChanged, this, &PlaybackStatePersistence::scheduleSave);
    connect(&player, &PlayerController::queueChanged, this, &PlaybackStatePersistence::scheduleSave);
    connect(&player, &PlayerController::stateChanged, this, &PlaybackStatePersistence::onPlayerStateChanged);
    connect(&player, &PlayerController::positionChanged, this, &PlaybackStatePersistence::onPositionChanged);
    connect(&player, &PlayerController::trackLeft, this, &PlaybackStatePersistence::onTrackLeft);
    connect(&player, &PlayerController::trackEnded, this, &PlaybackStatePersistence::onTrackEnded);
}

PlaybackStatePersistence::~PlaybackStatePersistence() = default;

void PlaybackStatePersistence::restoreAtLaunch()
{
    if (m_restoreStarted || m_shutDown) {
        return;
    }
    m_restoreStarted = true;
    m_file.readAsync().then(this, [this](const std::optional<PlaybackState>& state) { applyReadState(state); });
}

void PlaybackStatePersistence::shutdown()
{
    if (m_shutDown) {
        return;
    }
    saveResumePointNow();
    // Before the restore has run, the file still holds the previous
    // session's song; an empty player must not replace it.
    if (m_restoreFinished) {
        saveNow();
    }
    m_saveTimer->stop();
    m_shutDown = true;
    m_file.flush();
}

void PlaybackStatePersistence::applyReadState(const std::optional<PlaybackState>& state)
{
    if (m_shutDown) {
        return;
    }
    if (!state) {
        finishRestore();
        return;
    }
    QList<Track> history = m_services.library.loadHistoryTracks(true, true);
    history = migrateLegacyHomeHistory(*state, std::move(history), m_services.settings, m_services.tracks);

    const std::optional<Track> lastTrack = findLastTrack(*state, history);
    if (!lastTrack || !hasPlayableSource(*lastTrack) || m_services.player.currentTrack()) {
        finishRestore();
        return;
    }
    // A song whose file was deleted is not shown again.
    QtConcurrent::run([track = *lastTrack]() { return isLocalFileMissing(track); })
        .then(this, [this, saved = *state, track = *lastTrack](bool missing) {
            if (!missing && !m_shutDown) {
                applyRestoredTrack(saved, track);
            }
            finishRestore();
        });
}

void PlaybackStatePersistence::applyRestoredTrack(const PlaybackState& state, const Track& track)
{
    PlayerController& player = m_services.player;
    if (player.currentTrack()) {
        return; // something was started meanwhile
    }
    // Downloaded copies saved by earlier versions come back as the account
    // songs they are (the Offline page now hands those out).
    PlaybackState saved = state;
    Track current = track;
    if (holdsDownloadedCopies(saved, current)) {
        restoreAccountSongs(saved, current, m_services.library.loadTracks());
    }
    const QueueEngine queue =
        restoredQueue(saved, current, player.shuffle(), player.repeatMode(), systemQueueRandom());
    player.restore(queue, current, resumeStartSeconds(current));
}

void PlaybackStatePersistence::finishRestore()
{
    if (m_shutDown) {
        return;
    }
    m_restoreFinished = true;
    // Rewritten at once, as earlier versions did, which also drops the old
    // keys once their history has moved.
    saveNow();
    emit launchRestoreFinished();
}

void PlaybackStatePersistence::scheduleSave()
{
    if (m_shutDown) {
        return;
    }
    m_saveTimer->start(SaveDelay);
}

void PlaybackStatePersistence::saveNow()
{
    m_saveTimer->stop();
    if (m_shutDown) {
        return;
    }
    if (!m_file.write(currentState())) {
        qCDebug(lcPersistence) << "Playback state not saved while user data is being cleared.";
    }
}

void PlaybackStatePersistence::saveResumePointNow()
{
    const std::optional<Track>& current = m_services.player.currentTrack();
    if (m_shutDown || !current || !somethingOpen()) {
        return;
    }
    m_resume.saveNow(*current, m_services.player.positionSeconds(), m_services.player.durationSeconds());
}

void PlaybackStatePersistence::onPlayerStateChanged()
{
    // Pausing is the likeliest moment to walk away, so the exact point goes
    // in now rather than with the next periodic save.
    if (m_services.player.state() == PlayerController::State::Paused) {
        saveResumePointNow();
    }
}

void PlaybackStatePersistence::onPositionChanged()
{
    const std::optional<Track>& current = m_services.player.currentTrack();
    if (m_shutDown || !current || !somethingOpen()) {
        return;
    }
    m_resume.observe(*current, m_services.player.positionSeconds(), m_services.player.durationSeconds());
}

void PlaybackStatePersistence::onTrackLeft(const Track& track, double positionSeconds, double durationSeconds)
{
    // A song shown but never opened (the launch restore) keeps its point.
    if (m_shutDown || !somethingOpen()) {
        return;
    }
    m_resume.saveNow(track, positionSeconds, durationSeconds);
}

void PlaybackStatePersistence::onTrackEnded(const Track& track)
{
    if (m_shutDown) {
        return;
    }
    m_resume.clear(track);
}

bool PlaybackStatePersistence::somethingOpen() const
{
    return m_services.player.state() != PlayerController::State::Idle;
}

PlaybackState PlaybackStatePersistence::currentState() const
{
    const PlayerController& player = m_services.player;
    PlaybackState state;
    state.volume = player.volume();
    const std::optional<Track>& current = player.currentTrack();
    if (!current) {
        return state; // nothing playing: no song and no queue to bring back
    }
    state.lastTrack = lastTrackValue(*current);
    QueueSnapshotLimits limits;
    // Played songs are trimmed unless repeat-all plays them again.
    if (player.repeatMode() != RepeatMode::All) {
        limits.played = SavedPlayedLimit;
    }
    state.queue = SavedQueue{player.queue().snapshot(limits), *current, true};
    return state;
}

} // namespace lmp::core
