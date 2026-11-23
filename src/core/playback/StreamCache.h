#pragma once

#include "core/async/Result.h"
#include "core/library/UserDataOperationGate.h"

#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>
#include <QUrl>

#include <cstdint>
#include <memory>

namespace lmp::core {

struct StreamCacheUsage {
    quint64 bytes = 0;
    qsizetype files = 0;
};

// Fetches a stream into a file. Answers with the file name extension that
// suits the content type (".mp3", ".m4a", ...); a failure when the transfer
// did not complete. The file at `partPath` may be left behind on failure.
class IStreamDownloader {
public:
    virtual ~IStreamDownloader() = default;
    [[nodiscard]] virtual QFuture<Result<QString>> download(const QUrl& streamUrl, const QString& partPath) = 0;

protected:
    IStreamDownloader() = default;
    IStreamDownloader(const IStreamDownloader&) = default;
    IStreamDownloader& operator=(const IStreamDownloader&) = default;
};

// The extension for a stream's Content-Type, as the earlier versions chose
// it: mpeg/mp3 ".mp3", mp4/m4a/aac ".m4a", webm ".webm", ogg/opus ".ogg",
// wav ".wav", flac ".flac", anything else ".bin".
[[nodiscard]] QString streamExtensionForContentType(const QString& contentType);

// Songs fetched ahead in API key mode, so the next ones play from a whole
// file on disk and network hiccups cannot stall them (the earlier versions'
// stream cache, same folder and file names). A file is named
// "<hash>.<generation>.<attempt><extension>", the hash being the earlier
// versions' 64-bit FNV-1a of the song's stream cache key; it is written as
// ".part" and becomes visible only when complete. At most two transfers run
// at once (transfers made stale by invalidateInFlight still count until
// they return); the folder keeps at most 24 files and 256 MiB, the oldest
// going first.
//
// Every transfer holds a lease of the user data gate, so "Delete all data"
// waits for it. A transfer that returns after invalidateInFlight or clear
// is thrown away and never replaces a newer file.
//
// GUI thread only. The downloader must outlive the cache.
class StreamCache : public QObject {
    Q_OBJECT

public:
    static constexpr int MaxInFlight = 2;
    static constexpr qsizetype MaxFiles = 24;
    static constexpr quint64 MaxBytes = 256ULL * 1024 * 1024;

    StreamCache(QString folder, UserDataOperationGate& gate, IStreamDownloader& downloader, QObject* parent = nullptr);

    // Starts fetching `streamUrl` for `sourceKey` unless it is cached, being
    // fetched, the gate is closed or two transfers already run.
    void prefetch(const QString& sourceKey, const QUrl& streamUrl);
    // The complete file for `sourceKey`, or empty. Also finds a file from an
    // earlier run.
    [[nodiscard]] QString readyPath(const QString& sourceKey);
    // Deletes ".part" leftovers and trims the folder to its limits.
    void pruneOnStartup();
    // Forgets every lookup and transfer; transfers still running are thrown
    // away when they return. Deletes nothing.
    void invalidateInFlight();
    // Deletes the folder. False while a transfer still runs, so a deletion
    // never races a write.
    [[nodiscard]] bool clear();
    [[nodiscard]] StreamCacheUsage usage() const;
    [[nodiscard]] int activeTransfers() const { return m_active; }

    // The earlier versions' file name hash of a key (16 lower-case hex digits).
    [[nodiscard]] static QString hashKey(const QString& sourceKey);

private:
    enum class Status { InFlight, Ready, Failed };
    struct Entry {
        Status status = Status::InFlight;
        QString path;
        std::uint64_t attempt = 0;
    };

    [[nodiscard]] QString findOnDisk(const QString& hash) const;
    void finish(const QString& sourceKey, std::uint64_t generation, std::uint64_t attempt, const QString& partPath,
                const Result<QString>& extension);
    void trimToCap();

    QString m_folder;
    UserDataOperationGate& m_gate;
    IStreamDownloader& m_downloader;
    QHash<QString, Entry> m_entries;
    int m_active = 0;
    std::uint64_t m_generation = 0;
    std::uint64_t m_nextAttempt = 1;
};

} // namespace lmp::core
