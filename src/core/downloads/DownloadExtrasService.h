#pragma once

#include "core/async/Result.h"
#include "core/downloads/DownloadManager.h"
#include "core/downloads/SongFileExtras.h"
#include "core/lyrics/LyricsService.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace lmp::core::downloads {

// Writes each finished download's cover, lyrics and names into its file, so
// any player shows them (SongFileExtras). New downloads and the ones made
// before this existed alike: one song at a time, StepGapMs apart, newest
// first, while the song's source can be asked. A song is noted done
// (DownloadItem::extrasVersion) once what its source has is written, even
// when that is no cover or no lyrics; a song whose cover or lyrics could
// not be fetched, or whose file could not be replaced (it was playing),
// is tried again after RetryAfterMs.
//
// Threading: GUI thread. The file work runs on a worker (Dependencies::write)
// and its result comes back here.
class DownloadExtrasService final : public QObject {
    Q_OBJECT

public:
    // Raised when what is written changes, so every song is written again.
    static constexpr int CurrentVersion = 1;
    static constexpr int StepGapMs = 1500;
    static constexpr int RetryAfterMs = 10 * 60 * 1000;

    struct Dependencies {
        DownloadManager& downloads;
        // Whether the song's cover and lyrics can be asked for now: a remote
        // source is chosen, online, and able to name this song's cover.
        std::function<bool(const DownloadItem&)> canFetch;
        // The cover's bytes. NotFound: the song has none.
        std::function<QFuture<Result<QByteArray>>(const DownloadItem&)> cover;
        std::function<QFuture<Result<lyrics::LyricsResult>>(const DownloadItem&)> lyrics;
        // The file is open for playback and is better left alone.
        std::function<bool(const QString& filePath)> inUse;
        // Writes on a worker; writeSongFileExtras when empty.
        std::function<QFuture<Result<void>>(const QString& filePath, const SongFileExtras& extras)> write;
        // The pause between songs (tests shorten it).
        int stepGapMs = StepGapMs;
    };

    // Every dependency must outlive this object.
    explicit DownloadExtrasService(Dependencies dependencies, QObject* parent = nullptr);

    // Follows the download list from now on.
    void start();
    // Looks again soon: the source, the connection or the playing song changed.
    void refresh();

    [[nodiscard]] bool busy() const noexcept { return !m_current.isEmpty(); }

signals:
    // A song's file got its extras (for tests and views that read the file).
    void written(const QString& filePath);

private:
    void schedule(int delayMs);
    void step();
    void fetchLyrics(const DownloadItem& item, const QByteArray& cover);
    void write(const DownloadItem& item, const QByteArray& cover, const lyrics::LyricsResult& words);
    void finish(const QString& filePath, bool done);
    [[nodiscard]] bool waiting(const QString& filePath) const;

    Dependencies m_deps;
    QTimer m_timer;
    bool m_started = false;
    // The file being worked on; empty when idle.
    QString m_current;
    // Files to leave until this time (a monotonic clock, ms).
    QHash<QString, qint64> m_retryAt;
};

} // namespace lmp::core::downloads
