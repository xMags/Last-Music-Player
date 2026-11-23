#include "core/library/LibraryScanner.h"

#include <QCollator>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <utility>

#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/ArtworkCache.h"
#include "core/model/CatalogSourceKey.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"
#include "core/text/CaseFold.h"
#include "platform/ITagReader.h"

namespace lmp::core {

namespace {

const QString kDefaultScanFormats = QStringLiteral(".mp3,.flac");

} // namespace

LibraryScanner::LibraryScanner(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_tags(dependencies.tags)
    , m_artwork(dependencies.artwork)
    , m_tracks(dependencies.tracks)
    , m_library(dependencies.library)
    , m_settings(dependencies.settings)
    , m_gate(dependencies.gate)
{
    m_worker.setMaxThreadCount(1);
}

LibraryScanner::~LibraryScanner()
{
    if (m_state) {
        m_state->cancelled = true;
    }
    m_worker.waitForDone();
}

QStringList LibraryScanner::scanExtensions(const QString& stored)
{
    QStringList extensions;
    for (const QString& part : stored.split(QLatin1Char(','))) {
        QString token = foldCaseAscii(part.trimmed());
        if (token.isEmpty()) {
            continue;
        }
        if (!token.startsWith(QLatin1Char('.'))) {
            token.prepend(QLatin1Char('.'));
        }
        extensions.append(token);
    }
    if (extensions.isEmpty()) {
        extensions = {QStringLiteral(".mp3"), QStringLiteral(".flac")};
    }
    return extensions;
}

QStringList LibraryScanner::enumerateFiles(const QString& folder, const QStringList& extensions)
{
    QStringList files;
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString suffix = foldCaseAscii(QStringLiteral(".") + it.fileInfo().suffix());
        if (extensions.contains(suffix)) {
            files.append(QDir::toNativeSeparators(path));
        }
    }
    // Earlier versions asked the system for the files ordered by name.
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(), collator);
    return files;
}

QString LibraryScanner::formatDuration(double seconds)
{
    const int whole = static_cast<int>(seconds);
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

Track LibraryScanner::trackFromTags(const QString& filePath, const platform::TrackTags& tags,
                                    const QString& artworkUrl)
{
    Track track;
    track.title = tags.title.isEmpty() ? QFileInfo(filePath).completeBaseName() : tags.title;
    track.artist = tags.artist.isEmpty() ? QStringLiteral("Unknown Artist") : tags.artist;
    track.album = tags.album.isEmpty() ? QStringLiteral("Unknown Album") : tags.album;
    track.genre = tags.genre.isEmpty() ? QStringLiteral("Unknown Genre") : tags.genre;
    track.filePath = filePath;
    track.durationSeconds = tags.durationSeconds;
    track.durationText = formatDuration(tags.durationSeconds);
    track.sourceKind = QStringLiteral("local");
    track.sourceLabel = QStringLiteral("Local");
    track.artworkUrl = artworkUrl;
    track.offline = true;

    // The later of created and changed, as earlier versions sorted "added".
    QDateTime added = tags.created;
    if (tags.modified.isValid() && (!added.isValid() || tags.modified > added)) {
        added = tags.modified;
    }
    if (added.isValid()) {
        track.dateAddedSortKey = static_cast<double>(added.toSecsSinceEpoch());
        track.dateAdded = QLocale::c().toString(added.toLocalTime(), QStringLiteral("MMM dd yyyy"));
    }
    return track;
}

bool LibraryScanner::start(const QString& folder)
{
    if (m_running || folder.isEmpty() || !QFileInfo(folder).isDir()) {
        return false;
    }
    std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return false;
    }
    m_lease = std::move(lease);
    m_running = true;
    m_state = std::make_shared<ScanState>();

    const QStringList extensions =
        scanExtensions(m_settings.string(settings::keys::ScanFormats, kDefaultScanFormats));
    const std::shared_ptr<ScanState> state = m_state;
    platform::ITagReader& tags = m_tags;
    const ArtworkCache& artwork = m_artwork;
    m_worker.start([this, state, folder, extensions, &tags, &artwork]() {
        const QStringList files = enumerateFiles(folder, extensions);
        const int total = static_cast<int>(files.size());
        QMetaObject::invokeMethod(this, [this, total]() { emit progress(0, total); }, Qt::QueuedConnection);

        QList<Track> read;
        read.reserve(files.size());
        int done = 0;
        for (const QString& path : files) {
            if (state->cancelled) {
                break;
            }
            const Result<platform::TrackTags> fileTags = tags.read(path);
            if (fileTags) {
                read.append(trackFromTags(path, fileTags.value(), artwork.ensure(path)));
            }
            ++done;
            if (done == total || done % ProgressStep == 0) {
                QMetaObject::invokeMethod(this, [this, done, total]() { emit progress(done, total); },
                                          Qt::QueuedConnection);
            }
        }
        std::optional<QList<Track>> result;
        if (!state->cancelled) {
            result = std::move(read);
        }
        QMetaObject::invokeMethod(
            this, [this, state, result = std::move(result)]() mutable { onFilesRead(state, std::move(result)); },
            Qt::QueuedConnection);
    });
    return true;
}

void LibraryScanner::cancel()
{
    if (m_state) {
        m_state->cancelled = true;
    }
}

void LibraryScanner::onFilesRead(const std::shared_ptr<ScanState>& state, std::optional<QList<Track>> tracks)
{
    if (state != m_state) {
        return;
    }
    if (!tracks) {
        finish(Outcome::Cancelled, 0);
        return;
    }
    emit writing();
    m_pending = std::move(*tracks);
    m_activeKeys.clear();
    m_activeKeys.reserve(m_pending.size());
    m_written = 0;
    m_tracks.beginLocalScan();
    writeNextBatch();
}

void LibraryScanner::writeNextBatch()
{
    const qsizetype end = std::min(m_written + WriteBatch, m_pending.size());
    for (; m_written < end; ++m_written) {
        Track& track = m_pending[m_written];
        const QString key = catalogSourceKey(track);
        if (key.isEmpty()) {
            continue;
        }
        // A song liked before keeps its like.
        track.liked = m_tracks.isLiked(key);
        if (m_tracks.upsertLocalTrack(track, key)) {
            m_activeKeys.append(key);
        }
    }
    if (m_written < m_pending.size()) {
        QTimer::singleShot(0, this, &LibraryScanner::writeNextBatch);
        return;
    }
    const int count = static_cast<int>(m_activeKeys.size());
    const bool completed = static_cast<bool>(m_tracks.completeLocalScan(m_activeKeys));
    finish(completed ? Outcome::Completed : Outcome::Failed, count);
}

void LibraryScanner::finish(Outcome outcome, int trackCount)
{
    m_pending.clear();
    m_activeKeys.clear();
    m_state.reset();
    m_lease.reset();
    m_running = false;
    emit finished(outcome, trackCount);
}

bool LibraryScanner::pruneMissingTracks()
{
    if (m_running) {
        return false;
    }
    std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return false;
    }
    const QList<Track> locals = m_library.loadTracks(false, true);
    if (locals.isEmpty()) {
        return true;
    }
    m_lease = std::move(lease);
    m_running = true;
    QList<QPair<QString, QString>> files;
    files.reserve(locals.size());
    for (const Track& track : locals) {
        files.append({track.filePath, catalogSourceKey(track)});
    }
    m_worker.start([this, files]() {
        QStringList survivors;
        survivors.reserve(files.size());
        for (const auto& [path, key] : files) {
            if (!key.isEmpty() && QFileInfo::exists(path)) {
                survivors.append(key);
            }
        }
        const int localCount = static_cast<int>(files.size());
        QMetaObject::invokeMethod(
            this, [this, survivors, localCount]() { onPruneChecked(survivors, localCount); }, Qt::QueuedConnection);
    });
    return true;
}

void LibraryScanner::onPruneChecked(const QStringList& survivors, int localCount)
{
    const int missing = localCount - static_cast<int>(survivors.size());
    // Every file still there: nothing to write.
    if (missing > 0 && m_tracks.completeLocalScan(survivors)) {
        emit pruned(missing);
    }
    m_lease.reset();
    m_running = false;
}

} // namespace lmp::core
