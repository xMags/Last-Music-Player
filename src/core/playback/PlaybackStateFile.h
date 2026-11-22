#pragma once

#include "core/playback/PlaybackStateSnapshot.h"

#include <QFuture>
#include <QString>
#include <QThreadPool>

#include <functional>
#include <memory>

namespace lmp::core {

class UserDataOperationGate;

// LastMusicState.json on disk: the last song, the volume and the saved
// queue (PlaybackStateSnapshot.h has the shape).
//
// Writes, as earlier versions wrote the file: each write takes the next
// version number on the calling thread and the file is written later on a
// worker, under a lock, only if no newer write was asked for meanwhile, so
// the newest write wins even when an older one finishes last. The text goes
// through QSaveFile, so the file is either the old one or the new one. A
// write is skipped while the user data gate is closed, and holds a lease on
// it until the file is written, so a clean-up waits for it.
//
// Reads run on the same worker, after the writes asked for before them. An
// unreadable or missing file reads as nothing; it is left where it is.
//
// The worker is one thread owned by this object. Nothing is cancelled:
// flush() (and the destructor) wait until every queued job has finished.
// Members are called from one thread; the jobs share only the version
// counter and the lock, which live as long as the last job.
class PlaybackStateFile final {
public:
    // Runs a write job somewhere else. Tests pass one that holds jobs back
    // to run them out of order; flush() then cannot wait for them.
    using Executor = std::function<void(std::function<void()> job)>;

    // `gate` must outlive this object.
    PlaybackStateFile(QString filePath, UserDataOperationGate& gate, Executor executor = {});
    ~PlaybackStateFile();

    PlaybackStateFile(const PlaybackStateFile&) = delete;
    PlaybackStateFile& operator=(const PlaybackStateFile&) = delete;

    [[nodiscard]] QString filePath() const { return m_filePath; }

    // Queues a write of `state`. False when the gate refused it.
    bool write(const PlaybackState& state);
    // The file as it is on disk, read on the worker. Local history songs
    // whose files are gone are marked there too. Ready with nothing when
    // the file is missing or unreadable.
    [[nodiscard]] QFuture<std::optional<PlaybackState>> readAsync();
    // Deletes the file (and a temporary file an earlier version left) and
    // makes every write still queued a no-op. False when a file could not be
    // deleted.
    [[nodiscard]] bool clear();
    // Waits until every job on the worker has finished.
    void flush();

    // The blocking read readAsync runs; usable from any thread.
    [[nodiscard]] static std::optional<PlaybackState> readNow(const QString& filePath);

private:
    struct Shared;

    QString m_filePath;
    UserDataOperationGate& m_gate;
    std::shared_ptr<Shared> m_shared;
    QThreadPool m_worker;
    Executor m_executor;
};

} // namespace lmp::core
