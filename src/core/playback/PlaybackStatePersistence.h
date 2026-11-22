#pragma once

#include "core/model/Track.h"
#include "core/playback/PlaybackClock.h"
#include "core/playback/PlaybackStateFile.h"
#include "core/playback/PlaybackStateSnapshot.h"
#include "core/playback/ResumePositionTracker.h"

#include <QObject>

#include <chrono>
#include <memory>
#include <optional>

namespace lmp::core {

class AppPaths;
class PlayerController;
class SettingsStore;
class UserDataOperationGate;
namespace db {
class LibraryQueries;
class TrackRepository;
} // namespace db

// What a PlaybackStatePersistence works with. Every one must outlive it and
// belong to its thread (the database objects are used for a few small writes).
struct PlaybackStatePersistenceServices {
    PlayerController& player;
    SettingsStore& settings;
    db::TrackRepository& tracks;
    db::LibraryQueries& library;
    UserDataOperationGate& userDataGate;
    PlaybackClock& clock;
};

// Keeps the player's state across restarts:
// - LastMusicState.json (PlaybackStateFile) gets the last song, the volume
//   and the queue, written SaveDelay after the song or the queue changes,
//   after the launch restore, and at shutdown;
// - the resume point of the song (ResumePositionTracker) follows its
//   position, is saved exactly when it pauses, is left or at shutdown, and
//   is cleared when it plays to its end;
// - at launch, restoreAtLaunch() reads the file, moves the old home history
//   into the database once, and shows the last song in the player without
//   opening it, with its queue; the first Play starts it at its resume point.
//
// Lives on the player's thread. File work runs on the state file's worker
// and a check for the last song's file on the global pool; their answers
// come back through continuations bound to this object, so none runs after
// it is gone. shutdown() is called once by the composition root: it saves
// what is pending and waits for the file to be written.
class PlaybackStatePersistence final : public QObject {
    Q_OBJECT

public:
    // A change is written once the state has been quiet this long.
    static constexpr std::chrono::milliseconds SaveDelay{500};
    // Played songs kept before the current one, unless repeat-all is on.
    static constexpr qint64 SavedPlayedLimit = 50;

    PlaybackStatePersistence(PlaybackStatePersistenceServices services, const AppPaths& paths,
                             QObject* parent = nullptr);
    ~PlaybackStatePersistence() override;

    // Once, at launch; launchRestoreFinished() follows.
    void restoreAtLaunch();
    // Saves the resume point and the state, then waits until the file is
    // written. Nothing is saved after it.
    void shutdown();

    [[nodiscard]] bool isRestoreFinished() const noexcept { return m_restoreFinished; }

signals:
    void launchRestoreFinished();

private:
    void applyReadState(const std::optional<PlaybackState>& state);
    void applyRestoredTrack(const PlaybackState& state, const Track& track);
    void finishRestore();
    void scheduleSave();
    void saveNow();
    void saveResumePointNow();
    void onPlayerStateChanged();
    void onPositionChanged();
    void onTrackLeft(const Track& track, double positionSeconds, double durationSeconds);
    void onTrackEnded(const Track& track);
    [[nodiscard]] bool somethingOpen() const;
    [[nodiscard]] PlaybackState currentState() const;

    PlaybackStatePersistenceServices m_services;
    PlaybackStateFile m_file;
    ResumePositionTracker m_resume;
    std::unique_ptr<PlaybackTimer> m_saveTimer;
    bool m_restoreStarted = false;
    bool m_restoreFinished = false;
    bool m_shutDown = false;
};

} // namespace lmp::core
