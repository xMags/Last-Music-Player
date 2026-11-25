#pragma once

#include "core/async/Result.h"
#include "core/downloads/DownloadPolicy.h"
#include "core/library/UserDataOperationGate.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>

namespace lmp::core {

class SettingsStore;

// One song to fetch for offline playback.
struct DownloadTrackRequest {
    // downloadStableKey: "download|<scope>\n<song key>" for a remote song.
    QString stableKey;
    QString sourceUrl;
    QString provider;
    QString title;
    QString artist;
    QString album;
    QString artworkUrl;
};

struct DownloadItem {
    QString id;
    QString stableKey;
    QString sourceUrl;
    QString provider;
    QString title;
    QString artist;
    QString album;
    QString artworkUrl;
    QString filePath;
    // The file's extension without the dot, once complete.
    QString format;
    QString error;
    DownloadItemState state = DownloadItemState::Queued;
    std::uint64_t bytesDownloaded = 0;
    std::uint64_t bytesTotal = 0;
    std::uint64_t bytesPerSecond = 0;
    qint64 finishedAtUnix = 0;
};

struct DownloadJob {
    QString id;
    QString title;
    QString subtitle;
    QString artworkUrl;
    QList<DownloadItem> items;
    // Cleared from the list ("Clear completed"); its files stay.
    bool hidden = false;
};

// What the downloads page shows: the visible jobs and the totals.
struct DownloadJobSummary {
    DownloadJob job;
    DownloadItemState state = DownloadItemState::Queued;
    std::uint64_t bytesDownloaded = 0;
    std::uint64_t bytesTotal = 0;
    std::uint64_t bytesPerSecond = 0;
    int completedItems = 0;
    QString error;
};

struct DownloadSnapshot {
    QList<DownloadJobSummary> jobs;
    QString rootFolder;
    std::uint64_t revision = 0;
    std::uint64_t offlineBytes = 0;
    int offlineTracks = 0;
    bool allPaused = false;
    // This run of the app only.
    int sessionCompletedTracks = 0;
    std::uint64_t sessionBytes = 0;
    std::uint64_t sessionAverageBytesPerSecond = 0;
    bool avoidMeteredNetworks = true;
    bool autoDownloadLiked = true;
    bool downloadOnBattery = false;
    bool keepRecentOffline = true;
};

// Streams a song into a file, appending from `offset` with a range request
// when it is not zero. `stop` is asked between pieces; `progress` gets the
// bytes in the file so far and the expected total (0 when unknown).
class IDownloadFetcher {
public:
    struct Outcome {
        // The transfer ended because `stop` said so.
        bool stopped = false;
        // The server honoured the range (206), so the file was appended to.
        bool appended = false;
        QString mediaType;
    };
    using Stop = std::function<bool()>;
    using Progress = std::function<void(std::uint64_t written, std::uint64_t total)>;

    virtual ~IDownloadFetcher() = default;
    [[nodiscard]] virtual QFuture<Result<Outcome>> fetch(const QUrl& url, const QString& filePath, std::uint64_t offset,
                                                         Stop stop, Progress progress) = 0;

protected:
    IDownloadFetcher() = default;
    IDownloadFetcher(const IDownloadFetcher&) = default;
    IDownloadFetcher& operator=(const IDownloadFetcher&) = default;
};

// The offline downloads, as the earlier versions kept them: Downloads.json
// (schemaVersion 1: allPaused and the jobs with their items, the same field
// names and state words), partial files ".lmp-part-<item id>" in the
// download folder that a later attempt resumes with a range request, and
// finished files "<title, made safe>-<last 8 of the item id><ext>". At most
// two transfers run at once; nothing starts while all is paused, on a
// metered connection when that is avoided, on battery unless allowed, or
// while "Delete all data" runs. An item queued under another account fails
// with the earlier message. A stream address is asked for at the start of
// every attempt and never stored.
//
// Downloads.json is never overwritten with less than it held: a file that is
// there but does not read as the list (not JSON, another schemaVersion) is
// moved aside as "Downloads.json.corrupt-<UTC time>" before the list starts
// empty, and a file that cannot be opened (held by another program) blocks
// every save while the read is tried again, LoadRetries times, LoadRetryMs
// apart; songs queued meanwhile are kept beside what it held.
//
// GUI thread only.
class DownloadManager : public QObject {
    Q_OBJECT

public:
    static constexpr int MaxConcurrentTransfers = 2;
    static constexpr int LoadRetries = 5;
    static constexpr int LoadRetryMs = 2000;

    struct Dependencies {
        SettingsStore& settings;
        UserDataOperationGate& gate;
        IDownloadFetcher& fetcher;
        // Downloads.json, and the folder used when none is set.
        QString stateFile;
        QString defaultFolder;
        // A fresh stream address for the item.
        std::function<QFuture<Result<QUrl>>(const DownloadItem& item)> resolve;
        // "download|<scope>\n" for the current scope; items of another
        // scope cannot start.
        std::function<QString()> scopePrefix;
        // The connection and power state the rules check.
        std::function<bool()> metered;
        std::function<bool()> onBattery;
    };

    explicit DownloadManager(Dependencies dependencies, QObject* parent = nullptr);

    // Reads the settings and Downloads.json; a finished item whose file is
    // gone is queued again.
    void initialize();
    // Interrupted transfers go back to the queue; the state is saved.
    void shutdown();

    // False when nothing new was queued (every song is queued or offline).
    bool enqueue(const QString& title, const QString& subtitle, const QString& artworkUrl,
                 const QList<DownloadTrackRequest>& tracks);
    void pauseJob(const QString& jobId);
    void resumeJob(const QString& jobId);
    void cancelJob(const QString& jobId);
    void retryJob(const QString& jobId);
    // One song of a job: cancelling drops it (and its partial file; a job
    // left with no songs goes too), retrying queues it again if it failed.
    // A finished song is left alone.
    void cancelItem(const QString& jobId, const QString& itemId);
    void retryItem(const QString& jobId, const QString& itemId);
    void pauseAll();
    void resumeAll();
    void clearCompletedHistory();
    // Removes every download from this PC: running transfers are dropped
    // (their partial files go when they return), partial files go and
    // finished files are deleted. A finished file that cannot be deleted
    // (one that is playing, say) keeps its item, so it stays listed. False
    // when any file stayed, or while "Delete all data" runs (nothing
    // changes then).
    [[nodiscard]] bool removeAllDownloads();
    void dismissFailed(const QString& jobId);
    // One song, wherever it is listed: its finished file is deleted (or its
    // transfer dropped and partial file removed) and it leaves the list; a
    // job left with no songs goes too. False when its file could not be
    // deleted (it stays listed), or while "Delete all data" runs.
    [[nodiscard]] bool removeSong(const QString& stableKey);
    // Queues the song again wherever it failed.
    void retrySong(const QString& stableKey);
    // Starts what the rules now allow (after a connection or power change).
    void refreshScheduling();
    // False while a transfer runs or when the folder cannot be made.
    bool setRootFolder(const QString& folder);

    void setAvoidMeteredNetworks(bool value);
    void setAutoDownloadLiked(bool value);
    void setDownloadOnBattery(bool value);
    void setKeepRecentOffline(bool value);

    [[nodiscard]] DownloadSnapshot snapshot() const;
    // The finished file for `stableKey`, or empty.
    [[nodiscard]] QString readyPath(const QString& stableKey) const;
    [[nodiscard]] int running() const { return m_running; }

    // A file name made safe as the earlier versions did it: control
    // characters and <>:"/\|?* become "_", trailing spaces and dots go,
    // "Track" when nothing is left, at most 80 characters.
    [[nodiscard]] static QString sanitizeFileName(QString value);

signals:
    void changed();

private:
    struct Runtime {
        std::uint64_t attempt = 0;
        bool pauseRequested = false;
    };

    [[nodiscard]] DownloadJob* findJob(const QString& jobId);
    [[nodiscard]] DownloadItem* findItem(const QString& jobId, const QString& itemId);
    [[nodiscard]] bool schedulingAllowed() const;
    void pump();
    void start(const QString& jobId, const QString& itemId, UserDataOperationGate::Lease lease);
    void finishTransfer(const QString& jobId, const QString& itemId, std::uint64_t generation, std::uint64_t attempt,
                        const QString& partPath, const QString& finalPath, const QString& format,
                        const Result<IDownloadFetcher::Outcome>& outcome, qint64 startedMs);
    void refreshUsage();
    enum class LoadOutcome { Loaded, Missing, Unopenable, Unreadable };
    // Reads Downloads.json without changing anything here.
    [[nodiscard]] LoadOutcome readState(QList<DownloadJob>& jobs, bool& allPaused) const;
    void load();
    void retryLoad();
    // Moves an unreadable list aside; false when it stays where it is.
    [[nodiscard]] bool quarantineState() const;
    // A finished song whose file is gone is queued again.
    void requeueMissingFiles();
    void save();
    void touch();

    Dependencies m_deps;
    QList<DownloadJob> m_jobs;
    // Per item id: the current attempt and whether a pause was asked for.
    QHash<QString, Runtime> m_runtime;
    QString m_root;
    std::uint64_t m_revision = 1;
    std::uint64_t m_generation = 1;
    std::uint64_t m_offlineBytes = 0;
    int m_offlineTracks = 0;
    int m_sessionCompleted = 0;
    std::uint64_t m_sessionBytes = 0;
    double m_sessionSeconds = 0.0;
    int m_running = 0;
    bool m_initialized = false;
    bool m_shutdown = false;
    bool m_allPaused = false;
    bool m_avoidMetered = true;
    bool m_autoLiked = true;
    bool m_onBattery = false;
    bool m_keepRecent = true;
    // Downloads.json is there but could not be read yet: nothing is saved.
    bool m_saveBlocked = false;
    int m_loadRetries = 0;
};

} // namespace lmp::core
