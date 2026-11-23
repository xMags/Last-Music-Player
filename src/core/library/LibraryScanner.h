#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <memory>
#include <optional>

#include "core/library/UserDataOperationGate.h"
#include "core/model/Track.h"

namespace lmp::platform {
class ITagReader;
struct TrackTags;
} // namespace lmp::platform

namespace lmp::core::db {
class LibraryQueries;
class TrackRepository;
} // namespace lmp::core::db

namespace lmp::core {

class ArtworkCache;
class SettingsStore;

// Scans the music folder into the library, the way earlier versions did:
// every file under the folder whose extension is in ScanFormats, its tags
// (with "Unknown Artist", "Unknown Album" and "Unknown Genre" and the file
// name standing in for missing ones), its date added (the later of created
// and changed), its cover in the artwork cache, then one upsert per song
// under the source key "local|<lower-case path>". The scan ends by marking
// every local song it did not see inactive. Rows are never deleted, so
// likes, plays and playlists survive a file that comes back.
//
// Reading files runs on a worker of its own; the database is written on the
// thread that owns this object (the database's owner), a few hundred songs
// per turn of the event loop so the window keeps drawing. One scan at a time;
// a scan holds a UserDataOperationGate lease from start to finish.
class LibraryScanner : public QObject {
    Q_OBJECT

public:
    enum class Outcome {
        Completed,
        Cancelled,
        // The folder could not be read or the database refused the songs.
        Failed,
    };
    Q_ENUM(Outcome)

    // Progress is reported every this many files, and for the last one.
    static constexpr int ProgressStep = 25;
    // Songs written to the database per turn of the event loop.
    static constexpr int WriteBatch = 200;

    struct Dependencies {
        platform::ITagReader& tags;
        ArtworkCache& artwork;
        db::TrackRepository& tracks;
        db::LibraryQueries& library;
        SettingsStore& settings;
        UserDataOperationGate& gate;
    };

    // Every dependency must outlive this object.
    explicit LibraryScanner(const Dependencies& dependencies, QObject* parent = nullptr);
    // Cancels a running scan and waits for its worker.
    ~LibraryScanner() override;

    LibraryScanner(const LibraryScanner&) = delete;
    LibraryScanner& operator=(const LibraryScanner&) = delete;

    [[nodiscard]] bool isRunning() const { return m_running; }

    // Starts a scan of `folder`. False (and nothing starts) while another
    // scan or a prune runs, while user data is being cleaned up, or when the
    // folder does not exist.
    bool start(const QString& folder);
    // Stops a scan that is still reading files; the database is left as it
    // was. Writing the songs, once started, finishes.
    void cancel();

    // At launch: marks inactive every local song whose file is gone (a
    // folder scan does it too, but only when the folder still exists).
    // Checks the files on the worker. False when a scan runs.
    bool pruneMissingTracks();

    // The extensions in ScanFormats: comma-separated, each trimmed, lower
    // case and given a leading dot; ".mp3" and ".flac" when none is left.
    [[nodiscard]] static QStringList scanExtensions(const QString& stored);
    // Every file under `folder` with one of `extensions` (lower case, with
    // the dot), in path order without regard to case.
    [[nodiscard]] static QStringList enumerateFiles(const QString& folder, const QStringList& extensions);
    // The song for a file from its tags and cover address, with the fallbacks.
    [[nodiscard]] static Track trackFromTags(const QString& filePath, const platform::TrackTags& tags,
                                             const QString& artworkUrl);
    // "m:ss", as earlier versions showed lengths.
    [[nodiscard]] static QString formatDuration(double seconds);

signals:
    // `done` of `total` files read.
    void progress(int done, int total);
    // Reading is over and the songs are being written; Cancel no longer applies.
    void writing();
    void finished(lmp::core::LibraryScanner::Outcome outcome, int trackCount);
    // A launch prune marked songs inactive.
    void pruned(int missingCount);

private:
    struct ScanState {
        std::atomic<bool> cancelled{false};
    };

    void onFilesRead(const std::shared_ptr<ScanState>& state, std::optional<QList<Track>> tracks);
    void writeNextBatch();
    void finish(Outcome outcome, int trackCount);
    void onPruneChecked(const QStringList& survivors, int localCount);

    platform::ITagReader& m_tags;
    ArtworkCache& m_artwork;
    db::TrackRepository& m_tracks;
    db::LibraryQueries& m_library;
    SettingsStore& m_settings;
    UserDataOperationGate& m_gate;

    QThreadPool m_worker;
    bool m_running = false;
    std::shared_ptr<ScanState> m_state;
    std::optional<UserDataOperationGate::Lease> m_lease;
    QList<Track> m_pending;
    QStringList m_activeKeys;
    qsizetype m_written = 0;
};

} // namespace lmp::core
