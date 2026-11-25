#include "core/downloads/DownloadManager.h"

#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QLoggingCategory>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <algorithm>

namespace lmp::core {

namespace keys = settings::keys;

namespace {

Q_LOGGING_CATEGORY(lcDownloads, "lmp.downloads")

[[nodiscard]] QString newId()
{
    // The earlier versions' form: a GUID's 32 hex digits, upper case.
    return QUuid::createUuid().toString(QUuid::Id128).toUpper();
}

[[nodiscard]] QString stateName(DownloadItemState state)
{
    switch (state) {
    case DownloadItemState::Downloading:
        return QStringLiteral("downloading");
    case DownloadItemState::Paused:
        return QStringLiteral("paused");
    case DownloadItemState::Completed:
        return QStringLiteral("completed");
    case DownloadItemState::Failed:
        return QStringLiteral("failed");
    case DownloadItemState::Queued:
        break;
    }
    return QStringLiteral("queued");
}

[[nodiscard]] DownloadItemState parseState(const QString& value)
{
    if (value == QLatin1StringView("paused")) {
        return DownloadItemState::Paused;
    }
    if (value == QLatin1StringView("completed")) {
        return DownloadItemState::Completed;
    }
    if (value == QLatin1StringView("failed")) {
        return DownloadItemState::Failed;
    }
    // An interrupted transfer is queued again on recovery.
    return DownloadItemState::Queued;
}

[[nodiscard]] QString jsonText(const QJsonObject& object, QLatin1StringView key)
{
    const QJsonValue value = object.value(key);
    return value.isString() ? value.toString() : QString();
}

[[nodiscard]] std::uint64_t jsonCount(const QJsonObject& object, QLatin1StringView key)
{
    const double value = object.value(key).toDouble(0);
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

[[nodiscard]] DownloadItemState aggregate(const DownloadJob& job)
{
    QList<DownloadItemState> states;
    states.reserve(job.items.size());
    for (const DownloadItem& item : job.items) {
        states.append(item.state);
    }
    return aggregateDownloadState(states);
}

[[nodiscard]] QString partPathFor(const QString& root, const QString& itemId)
{
    return QDir(root).filePath(QStringLiteral(".lmp-part-") + itemId);
}

} // namespace

QString DownloadManager::sanitizeFileName(QString value)
{
    static const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (QChar& character : value) {
        if (character.unicode() < 32 || forbidden.contains(character)) {
            character = QLatin1Char('_');
        }
    }
    while (!value.isEmpty() && (value.endsWith(QLatin1Char(' ')) || value.endsWith(QLatin1Char('.')))) {
        value.chop(1);
    }
    if (value.isEmpty()) {
        value = QStringLiteral("Track");
    }
    if (value.size() > 80) {
        value.truncate(80);
    }
    return value;
}

DownloadManager::DownloadManager(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
}

void DownloadManager::touch()
{
    ++m_revision;
    emit changed();
}

void DownloadManager::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    const QString configured = m_deps.settings.string(keys::DownloadFolder, QString());
    m_root = configured.isEmpty() ? m_deps.defaultFolder : configured;
    m_avoidMetered = m_deps.settings.boolean(keys::DownloadOnlyOnWifi, true);
    m_autoLiked = m_deps.settings.boolean(keys::AutoDownloadLiked, true);
    m_onBattery = m_deps.settings.boolean(keys::DownloadOnBattery, false);
    m_keepRecent = m_deps.settings.boolean(keys::KeepRecentOffline, true);
    load();
    QDir().mkpath(m_root);
    requeueMissingFiles();
    refreshUsage();
    // What was read is news to every view built before this ran.
    touch();
    pump();
}

void DownloadManager::shutdown()
{
    if (m_shutdown) {
        return;
    }
    m_shutdown = true;
    ++m_generation;
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (item.state == DownloadItemState::Downloading) {
                m_runtime[item.id].pauseRequested = true;
                item.state = DownloadItemState::Queued;
                item.bytesPerSecond = 0;
            }
        }
    }
    save();
    touch();
}

DownloadJob* DownloadManager::findJob(const QString& jobId)
{
    for (DownloadJob& job : m_jobs) {
        if (job.id == jobId) {
            return &job;
        }
    }
    return nullptr;
}

DownloadItem* DownloadManager::findItem(const QString& jobId, const QString& itemId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return nullptr;
    }
    for (DownloadItem& item : job->items) {
        if (item.id == itemId) {
            return &item;
        }
    }
    return nullptr;
}

bool DownloadManager::enqueue(const QString& title, const QString& subtitle, const QString& artworkUrl,
                              const QList<DownloadTrackRequest>& tracks)
{
    initialize();
    QSet<QString> existing;
    for (const DownloadJob& job : std::as_const(m_jobs)) {
        for (const DownloadItem& item : job.items) {
            if (item.state != DownloadItemState::Failed) {
                existing.insert(item.stableKey);
            }
        }
    }
    DownloadJob job;
    job.id = newId();
    job.title = title.isEmpty() ? QStringLiteral("Download") : title;
    job.subtitle = subtitle;
    job.artworkUrl = artworkUrl;
    for (const DownloadTrackRequest& track : tracks) {
        if (track.stableKey.isEmpty() || track.sourceUrl.isEmpty() || existing.contains(track.stableKey)) {
            continue;
        }
        existing.insert(track.stableKey);
        DownloadItem item;
        item.id = newId();
        item.stableKey = track.stableKey;
        item.sourceUrl = track.sourceUrl;
        item.provider = track.provider;
        item.title = track.title;
        item.artist = track.artist;
        item.album = track.album;
        item.artworkUrl = track.artworkUrl;
        job.items.append(item);
    }
    if (job.items.isEmpty()) {
        return false;
    }
    m_jobs.append(job);
    save();
    touch();
    pump();
    return true;
}

void DownloadManager::pauseJob(const QString& jobId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return;
    }
    for (DownloadItem& item : job->items) {
        if (item.state == DownloadItemState::Downloading) {
            m_runtime[item.id].pauseRequested = true;
        } else if (item.state == DownloadItemState::Queued) {
            item.state = DownloadItemState::Paused;
        }
    }
    save();
    touch();
}

void DownloadManager::resumeJob(const QString& jobId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return;
    }
    for (DownloadItem& item : job->items) {
        if (item.state == DownloadItemState::Paused) {
            m_runtime[item.id].pauseRequested = false;
            item.state = DownloadItemState::Queued;
        } else if (item.state == DownloadItemState::Downloading) {
            m_runtime[item.id].pauseRequested = false;
        }
    }
    save();
    touch();
    pump();
}

void DownloadManager::cancelJob(const QString& jobId)
{
    const auto found =
        std::find_if(m_jobs.begin(), m_jobs.end(), [&jobId](const DownloadJob& job) { return job.id == jobId; });
    if (found == m_jobs.end()) {
        return;
    }
    for (const DownloadItem& item : found->items) {
        if (item.state != DownloadItemState::Completed) {
            QFile::remove(partPathFor(m_root, item.id));
        }
        // A transfer still running for it is dropped when it returns.
        m_runtime.remove(item.id);
    }
    m_jobs.erase(found);
    save();
    touch();
}

void DownloadManager::cancelItem(const QString& jobId, const QString& itemId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return;
    }
    const auto item = std::find_if(job->items.begin(), job->items.end(),
                                   [&itemId](const DownloadItem& candidate) { return candidate.id == itemId; });
    if (item == job->items.end() || item->state == DownloadItemState::Completed) {
        return;
    }
    QFile::remove(partPathFor(m_root, item->id));
    // A transfer still running for it is dropped when it returns.
    m_runtime.remove(item->id);
    job->items.erase(item);
    if (job->items.isEmpty()) {
        m_jobs.removeIf([&jobId](const DownloadJob& candidate) { return candidate.id == jobId; });
    }
    save();
    touch();
    pump();
}

void DownloadManager::retryItem(const QString& jobId, const QString& itemId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return;
    }
    for (DownloadItem& item : job->items) {
        if (item.id == itemId && item.state == DownloadItemState::Failed) {
            item.state = DownloadItemState::Queued;
            item.error.clear();
            m_runtime[item.id].pauseRequested = false;
            save();
            touch();
            pump();
            return;
        }
    }
}

void DownloadManager::retryJob(const QString& jobId)
{
    DownloadJob* job = findJob(jobId);
    if (job == nullptr) {
        return;
    }
    for (DownloadItem& item : job->items) {
        if (item.state == DownloadItemState::Failed) {
            item.state = DownloadItemState::Queued;
            item.error.clear();
            m_runtime[item.id].pauseRequested = false;
        }
    }
    save();
    touch();
    pump();
}

void DownloadManager::pauseAll()
{
    m_allPaused = true;
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (item.state == DownloadItemState::Downloading) {
                m_runtime[item.id].pauseRequested = true;
            } else if (item.state == DownloadItemState::Queued) {
                item.state = DownloadItemState::Paused;
            }
        }
    }
    save();
    touch();
}

void DownloadManager::resumeAll()
{
    m_allPaused = false;
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (item.state == DownloadItemState::Paused) {
                m_runtime[item.id].pauseRequested = false;
                item.state = DownloadItemState::Queued;
            } else if (item.state == DownloadItemState::Downloading) {
                m_runtime[item.id].pauseRequested = false;
            }
        }
    }
    save();
    touch();
    pump();
}

void DownloadManager::clearCompletedHistory()
{
    for (DownloadJob& job : m_jobs) {
        if (aggregate(job) == DownloadItemState::Completed) {
            job.hidden = true;
        }
    }
    save();
    touch();
}

bool DownloadManager::removeAllDownloads()
{
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return false;
    }
    bool allRemoved = true;
    // Two items can share one finished file; once it could not be deleted,
    // every item naming it stays.
    QSet<QString> kept;
    QList<DownloadJob> remaining;
    for (DownloadJob& job : m_jobs) {
        QList<DownloadItem> stay;
        for (const DownloadItem& item : std::as_const(job.items)) {
            m_runtime.remove(item.id);
            if (item.state != DownloadItemState::Completed || item.filePath.isEmpty()) {
                QFile::remove(partPathFor(m_root, item.id));
                continue;
            }
            const bool stuck = kept.contains(item.filePath)
                || (QFileInfo::exists(item.filePath) && !QFile::remove(item.filePath));
            if (stuck) {
                kept.insert(item.filePath);
                stay.append(item);
                allRemoved = false;
            }
        }
        if (!stay.isEmpty()) {
            job.items = stay;
            remaining.append(job);
        }
    }
    m_jobs = remaining;
    refreshUsage();
    save();
    touch();
    return allRemoved;
}

bool DownloadManager::removeSong(const QString& stableKey)
{
    if (stableKey.isEmpty()) {
        return true;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return false;
    }
    bool removed = true;
    bool changed = false;
    for (DownloadJob& job : m_jobs) {
        QList<DownloadItem> stay;
        for (const DownloadItem& item : std::as_const(job.items)) {
            if (item.stableKey != stableKey) {
                stay.append(item);
                continue;
            }
            changed = true;
            // A transfer still running for it is dropped when it returns.
            m_runtime.remove(item.id);
            if (item.state != DownloadItemState::Completed || item.filePath.isEmpty()) {
                QFile::remove(partPathFor(m_root, item.id));
                continue;
            }
            if (QFileInfo::exists(item.filePath) && !QFile::remove(item.filePath)) {
                // Playing, say: it stays listed.
                stay.append(item);
                removed = false;
            }
        }
        job.items = stay;
    }
    if (!changed) {
        return true;
    }
    m_jobs.removeIf([](const DownloadJob& job) { return job.items.isEmpty(); });
    refreshUsage();
    save();
    touch();
    pump();
    return removed;
}

void DownloadManager::retrySong(const QString& stableKey)
{
    bool changed = false;
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (item.stableKey == stableKey && item.state == DownloadItemState::Failed) {
                item.state = DownloadItemState::Queued;
                item.error.clear();
                m_runtime[item.id].pauseRequested = false;
                changed = true;
            }
        }
    }
    if (!changed) {
        return;
    }
    save();
    touch();
    pump();
}

void DownloadManager::dismissFailed(const QString& jobId)
{
    const auto failed = [&jobId](const DownloadJob& job) {
        return job.id == jobId && aggregate(job) == DownloadItemState::Failed;
    };
    for (const DownloadJob& job : std::as_const(m_jobs)) {
        if (failed(job)) {
            for (const DownloadItem& item : job.items) {
                QFile::remove(partPathFor(m_root, item.id));
            }
        }
    }
    m_jobs.removeIf(failed);
    save();
    touch();
}

void DownloadManager::refreshScheduling()
{
    initialize();
    pump();
}

bool DownloadManager::setRootFolder(const QString& folder)
{
    if (folder.trimmed().isEmpty() || m_running > 0) {
        return false;
    }
    if (!QDir().mkpath(folder) || !QFileInfo(folder).isDir()) {
        return false;
    }
    m_root = folder;
    m_deps.settings.setString(keys::DownloadFolder, folder);
    refreshUsage();
    save();
    touch();
    return true;
}

void DownloadManager::setAvoidMeteredNetworks(bool value)
{
    m_avoidMetered = value;
    m_deps.settings.setBoolean(keys::DownloadOnlyOnWifi, value);
    touch();
    pump();
}

void DownloadManager::setAutoDownloadLiked(bool value)
{
    m_autoLiked = value;
    m_deps.settings.setBoolean(keys::AutoDownloadLiked, value);
    touch();
}

void DownloadManager::setDownloadOnBattery(bool value)
{
    m_onBattery = value;
    m_deps.settings.setBoolean(keys::DownloadOnBattery, value);
    touch();
    pump();
}

void DownloadManager::setKeepRecentOffline(bool value)
{
    m_keepRecent = value;
    m_deps.settings.setBoolean(keys::KeepRecentOffline, value);
    touch();
}

DownloadSnapshot DownloadManager::snapshot() const
{
    DownloadSnapshot result;
    result.rootFolder = m_root;
    result.revision = m_revision;
    result.offlineBytes = m_offlineBytes;
    result.offlineTracks = m_offlineTracks;
    result.allPaused = m_allPaused;
    result.sessionCompletedTracks = m_sessionCompleted;
    result.sessionBytes = m_sessionBytes;
    result.sessionAverageBytesPerSecond =
        m_sessionSeconds > 0.0 ? static_cast<std::uint64_t>(static_cast<double>(m_sessionBytes) / m_sessionSeconds) : 0;
    result.avoidMeteredNetworks = m_avoidMetered;
    result.autoDownloadLiked = m_autoLiked;
    result.downloadOnBattery = m_onBattery;
    result.keepRecentOffline = m_keepRecent;
    for (const DownloadJob& source : m_jobs) {
        if (source.hidden) {
            continue;
        }
        DownloadJobSummary summary;
        summary.job = source;
        summary.state = aggregate(source);
        for (const DownloadItem& item : source.items) {
            summary.bytesDownloaded += item.bytesDownloaded;
            summary.bytesTotal += item.bytesTotal;
            summary.bytesPerSecond += item.bytesPerSecond;
            if (item.state == DownloadItemState::Completed) {
                ++summary.completedItems;
            }
            if (summary.error.isEmpty() && item.state == DownloadItemState::Failed) {
                summary.error = item.error;
            }
        }
        result.jobs.append(summary);
    }
    return result;
}

QString DownloadManager::readyPath(const QString& stableKey) const
{
    if (stableKey.isEmpty()) {
        return {};
    }
    for (const DownloadJob& job : m_jobs) {
        for (const DownloadItem& item : job.items) {
            if (item.stableKey == stableKey && item.state == DownloadItemState::Completed && !item.filePath.isEmpty()
                && QFileInfo(item.filePath).isFile()) {
                return item.filePath;
            }
        }
    }
    return {};
}

bool DownloadManager::schedulingAllowed() const
{
    DownloadSchedulingConditions conditions;
    conditions.avoidMeteredNetworks = m_avoidMetered;
    conditions.isMetered = m_avoidMetered && m_deps.metered && m_deps.metered();
    conditions.downloadOnBattery = m_onBattery;
    conditions.onBattery = m_deps.onBattery && m_deps.onBattery();
    conditions.allPaused = m_allPaused;
    conditions.shuttingDown = m_shutdown;
    return downloadSchedulingAllowed(conditions);
}

void DownloadManager::pump()
{
    if (!m_initialized || m_shutdown || m_allPaused || !schedulingAllowed()) {
        return;
    }
    const QString scopePrefix = m_deps.scopePrefix ? m_deps.scopePrefix() : QString();
    bool stranded = false;
    QList<std::pair<QString, QString>> starts;
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (m_running + starts.size() >= MaxConcurrentTransfers) {
                break;
            }
            if (item.state != DownloadItemState::Queued) {
                continue;
            }
            if (item.stableKey.startsWith(QLatin1StringView("download|"))
                && (scopePrefix.isEmpty() || !item.stableKey.startsWith(scopePrefix))) {
                // Its offline identity belongs to another account or source.
                item.state = DownloadItemState::Failed;
                item.error = QStringLiteral("Queued while a different account was signed in");
                item.bytesPerSecond = 0;
                stranded = true;
                continue;
            }
            starts.append({job.id, item.id});
        }
    }
    for (const auto& [jobId, itemId] : starts) {
        std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
        if (!lease) {
            break;
        }
        start(jobId, itemId, std::move(*lease));
    }
    if (stranded) {
        save();
        touch();
    }
}

void DownloadManager::start(const QString& jobId, const QString& itemId, UserDataOperationGate::Lease lease)
{
    DownloadItem* item = findItem(jobId, itemId);
    if (item == nullptr) {
        return;
    }
    item->state = DownloadItemState::Downloading;
    item->error.clear();
    item->bytesPerSecond = 0;
    Runtime& runtime = m_runtime[itemId];
    runtime.pauseRequested = false;
    const std::uint64_t attempt = ++runtime.attempt;
    const std::uint64_t generation = m_generation;
    ++m_running;
    save();
    touch();

    auto held = std::make_shared<UserDataOperationGate::Lease>(std::move(lease));
    const DownloadItem snapshotItem = *item;
    const QString root = m_root;
    const QString partPath = partPathFor(root, itemId);
    QElapsedTimer started;
    started.start();
    const qint64 startedMs = started.msecsSinceReference();
    const auto current = [this, jobId, itemId, generation, attempt]() {
        return generation == m_generation && findItem(jobId, itemId) != nullptr
            && m_runtime.value(itemId).attempt == attempt;
    };
    const auto fail = [this, jobId, itemId, generation, attempt, held](const QString& error) {
        finishTransfer(jobId, itemId, generation, attempt, {}, {}, {},
                       Result<IDownloadFetcher::Outcome>(Failure::of(FailureKind::Unknown, error)), 0);
        *held = UserDataOperationGate::Lease{};
    };
    if (!m_deps.resolve) {
        fail(QStringLiteral("Could not resolve this track"));
        return;
    }
    m_deps.resolve(snapshotItem)
        .then(this, [this, current, fail, held, jobId, itemId, generation, attempt, root, partPath, snapshotItem,
                     startedMs](const Result<QUrl>& stream) {
            if (!current()) {
                finishTransfer(jobId, itemId, generation, attempt, partPath, {}, {},
                               Result<IDownloadFetcher::Outcome>(Failure::of(FailureKind::Cancelled)), 0);
                *held = UserDataOperationGate::Lease{};
                return;
            }
            if (!stream || stream.value().isEmpty()) {
                fail(QStringLiteral("Could not resolve this track"));
                return;
            }
            if (!QDir().mkpath(root)) {
                fail(QStringLiteral("Download folder is unavailable"));
                return;
            }
            const QFileInfo part(partPath);
            const std::uint64_t existing = part.isFile() ? static_cast<std::uint64_t>(part.size()) : 0;
            QElapsedTimer sample;
            sample.start();
            auto lastBytes = std::make_shared<std::uint64_t>(existing);
            auto lastSample = std::make_shared<qint64>(sample.elapsed());
            m_deps.fetcher
                .fetch(stream.value(), partPath, existing,
                       [this, itemId, current]() {
                           return !current() || m_runtime.value(itemId).pauseRequested || m_allPaused;
                       },
                       [this, jobId, itemId, current, sample, lastBytes, lastSample](std::uint64_t written,
                                                                                    std::uint64_t total) {
                           if (!current()) {
                               return;
                           }
                           const qint64 now = sample.elapsed();
                           if (now - *lastSample < 400) {
                               return;
                           }
                           DownloadItem* live = findItem(jobId, itemId);
                           live->bytesDownloaded = written;
                           live->bytesTotal = total;
                           live->bytesPerSecond = (written - std::min(written, *lastBytes)) * 1000
                               / static_cast<std::uint64_t>(std::max<qint64>(1, now - *lastSample));
                           *lastBytes = written;
                           *lastSample = now;
                           touch();
                       })
                .then(this, [this, held, jobId, itemId, generation, attempt, root, partPath, snapshotItem,
                             startedMs](const Result<IDownloadFetcher::Outcome>& outcome) {
                    const QString format = outcome ? downloadExtensionForMediaType(outcome.value().mediaType) : QString();
                    const QString suffix = itemId.size() > 8 ? itemId.right(8) : itemId;
                    const QString finalPath =
                        QDir(root).filePath(sanitizeFileName(snapshotItem.title) + QLatin1Char('-') + suffix + format);
                    finishTransfer(jobId, itemId, generation, attempt, partPath, finalPath, format, outcome, startedMs);
                    *held = UserDataOperationGate::Lease{};
                });
        });
}

void DownloadManager::finishTransfer(const QString& jobId, const QString& itemId, std::uint64_t generation,
                                     std::uint64_t attempt, const QString& partPath, const QString& finalPath,
                                     const QString& format, const Result<IDownloadFetcher::Outcome>& outcome,
                                     qint64 startedMs)
{
    if (m_running > 0) {
        --m_running;
    }
    DownloadItem* item = findItem(jobId, itemId);
    const bool current = item != nullptr && generation == m_generation && m_runtime.value(itemId).attempt == attempt;
    if (!current) {
        // Cancelled or from before a shutdown: a cancelled item's part goes.
        if (item == nullptr && !partPath.isEmpty()) {
            QFile::remove(partPath);
        }
        pump();
        return;
    }
    Runtime& runtime = m_runtime[itemId];
    item->bytesPerSecond = 0;
    if (outcome && outcome.value().stopped) {
        item->state = DownloadItemState::Paused;
        runtime.pauseRequested = false;
    } else if (outcome) {
        QFile::remove(finalPath);
        if (!QFile::rename(partPath, finalPath)) {
            item->state = DownloadItemState::Failed;
            item->error = QStringLiteral("Could not publish the downloaded file");
        } else {
            item->state = DownloadItemState::Completed;
            item->filePath = finalPath;
            item->format = format.startsWith(QLatin1Char('.')) ? format.mid(1) : format;
            item->bytesDownloaded = static_cast<std::uint64_t>(QFileInfo(finalPath).size());
            item->bytesTotal = item->bytesDownloaded;
            item->error.clear();
            item->finishedAtUnix = QDateTime::currentSecsSinceEpoch();
            ++m_sessionCompleted;
            m_sessionBytes += item->bytesDownloaded;
            QElapsedTimer now;
            now.start();
            if (startedMs > 0) {
                m_sessionSeconds += static_cast<double>(now.msecsSinceReference() - startedMs) / 1000.0;
            }
            refreshUsage();
        }
    } else if (outcome.failure().kind != FailureKind::Cancelled) {
        item->state = DownloadItemState::Failed;
        item->error = outcome.failure().detail.isEmpty() ? QStringLiteral("The download was interrupted")
                                                         : outcome.failure().detail;
    } else {
        item->state = DownloadItemState::Queued;
    }
    save();
    touch();
    pump();
}

void DownloadManager::refreshUsage()
{
    m_offlineBytes = 0;
    m_offlineTracks = 0;
    QSet<QString> counted;
    for (const DownloadJob& job : std::as_const(m_jobs)) {
        for (const DownloadItem& item : job.items) {
            if (item.state != DownloadItemState::Completed || item.filePath.isEmpty() || counted.contains(item.filePath)) {
                continue;
            }
            counted.insert(item.filePath);
            const QFileInfo file(item.filePath);
            if (file.isFile()) {
                m_offlineBytes += static_cast<std::uint64_t>(file.size());
                ++m_offlineTracks;
            }
        }
    }
}

void DownloadManager::requeueMissingFiles()
{
    for (DownloadJob& job : m_jobs) {
        for (DownloadItem& item : job.items) {
            if (item.state == DownloadItemState::Completed
                && (item.filePath.isEmpty() || !QFileInfo(item.filePath).isFile())) {
                item.state = DownloadItemState::Queued;
                item.filePath.clear();
                item.format.clear();
                item.bytesDownloaded = 0;
                item.bytesTotal = 0;
                item.finishedAtUnix = 0;
                job.hidden = false;
            }
        }
    }
}

void DownloadManager::load()
{
    QList<DownloadJob> jobs;
    bool allPaused = false;
    switch (readState(jobs, allPaused)) {
    case LoadOutcome::Loaded:
        m_jobs = std::move(jobs);
        m_allPaused = allPaused;
        return;
    case LoadOutcome::Missing:
        return;
    case LoadOutcome::Unreadable:
        qCWarning(lcDownloads) << "The download list could not be read; it is set aside and starts empty.";
        // Kept from being overwritten until it is moved aside.
        if (!quarantineState()) {
            m_saveBlocked = true;
        }
        return;
    case LoadOutcome::Unopenable:
        qCWarning(lcDownloads) << "The download list could not be opened; it is read again shortly.";
        m_saveBlocked = true;
        m_loadRetries = 0;
        QTimer::singleShot(LoadRetryMs, this, &DownloadManager::retryLoad);
        return;
    }
}

void DownloadManager::retryLoad()
{
    if (m_shutdown || !m_saveBlocked) {
        return;
    }
    QList<DownloadJob> jobs;
    bool allPaused = false;
    const LoadOutcome outcome = readState(jobs, allPaused);
    if (outcome == LoadOutcome::Unopenable) {
        if (++m_loadRetries < LoadRetries) {
            QTimer::singleShot(LoadRetryMs, this, &DownloadManager::retryLoad);
        } else {
            qCWarning(lcDownloads) << "The download list still cannot be opened; changes are not saved this run.";
        }
        return;
    }
    if (outcome == LoadOutcome::Unreadable && !quarantineState()) {
        qCWarning(lcDownloads) << "The unreadable download list could not be set aside; changes are not saved this run.";
        return;
    }
    if (outcome == LoadOutcome::Loaded) {
        // What the file held first, then what was queued meanwhile.
        QSet<QString> known;
        for (const DownloadJob& job : std::as_const(jobs)) {
            known.insert(job.id);
        }
        for (const DownloadJob& job : std::as_const(m_jobs)) {
            if (!known.contains(job.id)) {
                jobs.append(job);
            }
        }
        m_jobs = std::move(jobs);
        m_allPaused = allPaused || m_allPaused;
        requeueMissingFiles();
        refreshUsage();
    }
    m_saveBlocked = false;
    save();
    touch();
    pump();
}

bool DownloadManager::quarantineState() const
{
    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
    const QString target = m_deps.stateFile + QStringLiteral(".corrupt-") + stamp;
    if (QFileInfo::exists(target)) {
        QFile::remove(target);
    }
    if (!QFile::rename(m_deps.stateFile, target)) {
        qCWarning(lcDownloads) << "The unreadable download list could not be set aside.";
        return false;
    }
    return true;
}

DownloadManager::LoadOutcome DownloadManager::readState(QList<DownloadJob>& jobs, bool& allPaused) const
{
    QFile file(m_deps.stateFile);
    if (!file.exists()) {
        return LoadOutcome::Missing;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return LoadOutcome::Unopenable;
    }
    const QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        return LoadOutcome::Missing; // an empty file held nothing
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return LoadOutcome::Unreadable;
    }
    const QJsonObject root = document.object();
    if (root.value(QLatin1StringView("schemaVersion")).toDouble(0) != 1) {
        return LoadOutcome::Unreadable;
    }
    allPaused = root.value(QLatin1StringView("allPaused")).toBool(false);
    for (const QJsonValue& jobValue : root.value(QLatin1StringView("jobs")).toArray()) {
        const QJsonObject object = jobValue.toObject();
        DownloadJob job;
        job.id = jsonText(object, QLatin1StringView("id"));
        job.title = jsonText(object, QLatin1StringView("title"));
        job.subtitle = jsonText(object, QLatin1StringView("subtitle"));
        job.artworkUrl = jsonText(object, QLatin1StringView("artworkUrl"));
        job.hidden = object.value(QLatin1StringView("hidden")).toBool(false);
        for (const QJsonValue& itemValue : object.value(QLatin1StringView("items")).toArray()) {
            const QJsonObject source = itemValue.toObject();
            DownloadItem item;
            item.id = jsonText(source, QLatin1StringView("id"));
            item.stableKey = jsonText(source, QLatin1StringView("stableKey"));
            item.sourceUrl = jsonText(source, QLatin1StringView("sourceUrl"));
            item.provider = jsonText(source, QLatin1StringView("provider"));
            item.title = jsonText(source, QLatin1StringView("title"));
            item.artist = jsonText(source, QLatin1StringView("artist"));
            item.album = jsonText(source, QLatin1StringView("album"));
            item.artworkUrl = jsonText(source, QLatin1StringView("artworkUrl"));
            item.filePath = jsonText(source, QLatin1StringView("filePath"));
            item.format = jsonText(source, QLatin1StringView("format"));
            item.error = jsonText(source, QLatin1StringView("error"));
            item.state = parseState(jsonText(source, QLatin1StringView("state")));
            item.bytesDownloaded = jsonCount(source, QLatin1StringView("bytesDownloaded"));
            item.bytesTotal = jsonCount(source, QLatin1StringView("bytesTotal"));
            item.finishedAtUnix = static_cast<qint64>(jsonCount(source, QLatin1StringView("finishedAt")));
            if (!item.id.isEmpty()) {
                job.items.append(item);
            }
        }
        if (!job.id.isEmpty()) {
            jobs.append(job);
        }
    }
    return LoadOutcome::Loaded;
}

void DownloadManager::save()
{
    // Never over a list that is there but was not read.
    if (m_saveBlocked) {
        return;
    }
    QJsonArray jobs;
    for (const DownloadJob& source : std::as_const(m_jobs)) {
        QJsonArray items;
        for (const DownloadItem& item : source.items) {
            items.append(QJsonObject{
                {QStringLiteral("id"), item.id},
                {QStringLiteral("stableKey"), item.stableKey},
                {QStringLiteral("sourceUrl"), item.sourceUrl},
                {QStringLiteral("provider"), item.provider},
                {QStringLiteral("title"), item.title},
                {QStringLiteral("artist"), item.artist},
                {QStringLiteral("album"), item.album},
                {QStringLiteral("artworkUrl"), item.artworkUrl},
                {QStringLiteral("filePath"), item.filePath},
                {QStringLiteral("format"), item.format},
                {QStringLiteral("error"), item.error},
                {QStringLiteral("state"), stateName(item.state)},
                {QStringLiteral("bytesDownloaded"), static_cast<double>(item.bytesDownloaded)},
                {QStringLiteral("bytesTotal"), static_cast<double>(item.bytesTotal)},
                {QStringLiteral("finishedAt"), static_cast<double>(item.finishedAtUnix)},
            });
        }
        jobs.append(QJsonObject{{QStringLiteral("id"), source.id},
                                {QStringLiteral("title"), source.title},
                                {QStringLiteral("subtitle"), source.subtitle},
                                {QStringLiteral("artworkUrl"), source.artworkUrl},
                                {QStringLiteral("hidden"), source.hidden},
                                {QStringLiteral("items"), items}});
    }
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("allPaused"), m_allPaused},
                           {QStringLiteral("jobs"), jobs}};
    QDir().mkpath(QFileInfo(m_deps.stateFile).absolutePath());
    QSaveFile file(m_deps.stateFile);
    if (!file.open(QIODevice::WriteOnly)) {
        qCWarning(lcDownloads) << "The download list could not be saved:" << file.errorString();
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        qCWarning(lcDownloads) << "The download list could not be saved:" << file.errorString();
    }
}

} // namespace lmp::core
