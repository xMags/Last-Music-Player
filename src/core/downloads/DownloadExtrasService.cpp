#include "core/downloads/DownloadExtrasService.h"

#include <QDeadlineTimer>
#include <QLoggingCategory>
#include <QtConcurrentRun>

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace lmp::core::downloads {

namespace {

Q_LOGGING_CATEGORY(lcExtras, "lmp.downloads.extras")

[[nodiscard]] qint64 nowMs()
{
    return QDeadlineTimer::current().deadline();
}

// A failure worth trying again later: the network or the service, not the song.
[[nodiscard]] bool passing(const Failure& failure)
{
    return failure.kind != FailureKind::NotFound;
}

} // namespace

DownloadExtrasService::DownloadExtrasService(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    if (!m_deps.write) {
        m_deps.write = [](const QString& filePath, const SongFileExtras& extras) {
            return QtConcurrent::run(&writeSongFileExtras, filePath, extras);
        };
    }
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &DownloadExtrasService::step);
}

void DownloadExtrasService::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    connect(&m_deps.downloads, &DownloadManager::changed, this, &DownloadExtrasService::refresh);
    schedule(m_deps.stepGapMs);
}

void DownloadExtrasService::refresh()
{
    if (m_started && !busy() && !m_timer.isActive()) {
        schedule(m_deps.stepGapMs);
    }
}

void DownloadExtrasService::schedule(int delayMs)
{
    m_timer.start(std::max(0, delayMs));
}

bool DownloadExtrasService::waiting(const QString& filePath) const
{
    const auto found = m_retryAt.constFind(filePath);
    return found != m_retryAt.constEnd() && found.value() > nowMs();
}

void DownloadExtrasService::step()
{
    if (busy()) {
        return;
    }
    std::optional<DownloadItem> next;
    qint64 earliestRetry = std::numeric_limits<qint64>::max();
    for (const DownloadItem& item : m_deps.downloads.finishedSongs()) {
        if (item.extrasVersion >= CurrentVersion) {
            continue;
        }
        if (waiting(item.filePath)) {
            earliestRetry = std::min(earliestRetry, m_retryAt.value(item.filePath));
            continue;
        }
        if (!m_deps.canFetch(item) || (m_deps.inUse && m_deps.inUse(item.filePath))) {
            continue; // asked again on refresh()
        }
        if (!next || item.finishedAtUnix > next->finishedAtUnix) {
            next = item;
        }
    }
    if (!next) {
        if (earliestRetry != std::numeric_limits<qint64>::max()) {
            schedule(static_cast<int>(std::min<qint64>(earliestRetry - nowMs(), RetryAfterMs)));
        }
        return;
    }
    m_current = next->filePath;
    const DownloadItem item = *next;
    if (item.artworkUrl.isEmpty()) {
        fetchLyrics(item, {});
        return;
    }
    m_deps.cover(item).then(this, [this, item](const Result<QByteArray>& cover) {
        if (!cover && passing(cover.failure())) {
            qCDebug(lcExtras) << "cover not fetched now:" << cover.failure().detail;
            finish(item.filePath, false);
            return;
        }
        fetchLyrics(item, cover ? cover.value() : QByteArray());
    });
}

void DownloadExtrasService::fetchLyrics(const DownloadItem& item, const QByteArray& cover)
{
    m_deps.lyrics(item).then(this, [this, item, cover](const Result<lyrics::LyricsResult>& words) {
        if (!words && passing(words.failure())) {
            qCDebug(lcExtras) << "lyrics not fetched now:" << words.failure().detail;
            finish(item.filePath, false);
            return;
        }
        write(item, cover, words ? words.value() : lyrics::LyricsResult{});
    });
}

void DownloadExtrasService::write(const DownloadItem& item, const QByteArray& cover, const lyrics::LyricsResult& words)
{
    // The song may have started playing while its extras were fetched.
    if (m_deps.inUse && m_deps.inUse(item.filePath)) {
        finish(item.filePath, false);
        return;
    }
    SongFileExtras extras;
    extras.title = item.title;
    extras.artist = item.artist;
    extras.album = item.album;
    extras.cover = cover;
    if (words.found && !words.instrumental) {
        extras.plainLyrics = words.plain;
        extras.syncedLyrics = words.synced;
    }
    const QString filePath = item.filePath;
    m_deps.write(filePath, extras).then(this, [this, filePath](const Result<void>& outcome) {
        if (!outcome) {
            qCDebug(lcExtras) << "extras not written:" << outcome.failure().detail;
            // A file that cannot hold tags never will; one in use may later.
            const bool never = outcome.failure().kind == FailureKind::NotAvailable;
            if (never) {
                m_deps.downloads.setExtrasWritten(filePath, CurrentVersion);
            }
            finish(filePath, never || outcome.failure().kind == FailureKind::NotFound);
            return;
        }
        m_deps.downloads.setExtrasWritten(filePath, CurrentVersion);
        emit written(filePath);
        finish(filePath, true);
    });
}

void DownloadExtrasService::finish(const QString& filePath, bool done)
{
    if (done) {
        m_retryAt.remove(filePath);
    } else {
        m_retryAt.insert(filePath, nowMs() + RetryAfterMs);
    }
    m_current.clear();
    schedule(m_deps.stepGapMs);
}

} // namespace lmp::core::downloads
