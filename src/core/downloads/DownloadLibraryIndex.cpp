#include "core/downloads/DownloadLibraryIndex.h"

#include "core/db/TrackRepository.h"
#include "core/downloads/DownloadManager.h"
#include "core/library/LibraryScanner.h"
#include "core/mode/SourceMode.h"
#include "core/model/CatalogSourceKey.h"
#include "platform/ITagReader.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QLatin1Char>
#include <QLatin1StringView>
#include <QLocale>
#include <QMetaObject>
#include <QStringList>

#include <algorithm>
#include <optional>
#include <utility>

namespace lmp::core::downloads {

namespace {

// One song per file: a song kept by two download jobs is one row, the one
// that finished last.
[[nodiscard]] QList<DownloadItem> songsByFile(const QList<DownloadItem>& finished)
{
    QHash<QString, DownloadItem> byFile;
    for (const DownloadItem& item : finished) {
        const auto known = byFile.constFind(item.filePath);
        if (known == byFile.constEnd() || item.finishedAtUnix > known->finishedAtUnix) {
            byFile.insert(item.filePath, item);
        }
    }
    QList<DownloadItem> songs = byFile.values();
    std::sort(songs.begin(), songs.end(),
              [](const DownloadItem& a, const DownloadItem& b) { return a.filePath < b.filePath; });
    return songs;
}

// What the rows would hold, to tell whether a write is needed.
[[nodiscard]] QByteArray signatureOf(const QList<DownloadItem>& songs)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const DownloadItem& item : songs) {
        for (const QString* field : {&item.filePath, &item.stableKey, &item.title, &item.artist, &item.album,
                                     &item.artworkUrl, &item.sourceUrl, &item.provider}) {
            hash.addData(field->toUtf8());
            hash.addData(QByteArrayView("\x1f", 1));
        }
        hash.addData(QByteArray::number(item.finishedAtUnix));
        hash.addData(QByteArrayView("\x1e", 1));
    }
    return hash.result();
}

} // namespace

DownloadLibraryIndex::DownloadLibraryIndex(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_downloads(dependencies.downloads)
    , m_tags(dependencies.tags)
    , m_tracks(dependencies.tracks)
    , m_gate(dependencies.gate)
    , m_settleMs(dependencies.settleMs)
{
    m_worker.setMaxThreadCount(1);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &DownloadLibraryIndex::run);
}

DownloadLibraryIndex::~DownloadLibraryIndex()
{
    // A queued answer for this object is dropped once it is gone.
    m_worker.waitForDone();
}

void DownloadLibraryIndex::start()
{
    connect(&m_downloads, &DownloadManager::changed, this, [this]() { schedule(m_settleMs); });
    schedule(0);
}

void DownloadLibraryIndex::reindex()
{
    m_written.clear();
    schedule(0);
}

void DownloadLibraryIndex::schedule(int delayMs)
{
    // A pending write keeps its time unless this one is sooner.
    if (m_timer.isActive() && m_timer.remainingTime() <= delayMs) {
        return;
    }
    m_timer.start(delayMs);
}

Track DownloadLibraryIndex::trackFor(const DownloadItem& item, double durationSeconds)
{
    Track track;
    // The fallbacks a folder scan uses for missing tags.
    track.title = item.title.isEmpty() ? QFileInfo(item.filePath).completeBaseName() : item.title;
    track.artist = item.artist.isEmpty() ? QStringLiteral("Unknown Artist") : item.artist;
    track.album = item.album.isEmpty() ? QStringLiteral("Unknown Album") : item.album;
    track.genre = QStringLiteral("Unknown Genre");
    track.filePath = item.filePath;
    track.sourceKind = QStringLiteral("local");
    track.sourceLabel = QStringLiteral("Local");
    // The address it came from, as the Offline page's rows keep it: the
    // rows show the download mark by it.
    track.sourceUrl = item.sourceUrl;
    track.sourceTag = item.provider;
    track.artworkUrl = item.artworkUrl;
    track.offline = true;
    track.durationSeconds = durationSeconds;
    track.durationText = LibraryScanner::formatDuration(durationSeconds);
    if (item.finishedAtUnix > 0) {
        const QDateTime finished = QDateTime::fromSecsSinceEpoch(item.finishedAtUnix);
        track.dateAddedSortKey = static_cast<double>(item.finishedAtUnix);
        track.dateAdded = QLocale::c().toString(finished.toLocalTime(), QStringLiteral("MMM dd yyyy"));
    }
    return track;
}

db::TrackRepository::DownloadedTrack DownloadLibraryIndex::downloadedFor(const DownloadItem& item,
                                                                         double durationSeconds)
{
    db::TrackRepository::DownloadedTrack song;
    song.track = trackFor(item, durationSeconds);
    const QLatin1StringView prefix("download|");
    const qsizetype end = item.stableKey.indexOf(QLatin1Char('\n'));
    if (!item.stableKey.startsWith(prefix) || end < 0) {
        return song;
    }
    const QStringList scope = item.stableKey.mid(prefix.size(), end - prefix.size()).split(QLatin1Char(':'));
    const QString kind = scope.value(0);
    if (kind == mode::sourceModeName(mode::SourceMode::Account)) {
        song.likeSource = db::TrackRepository::LikeSource::Account;
        // "Account:<owner>"; a key with run counters ("Account:3:7") names none.
        song.accountId = scope.size() == 2 ? scope.at(1) : QString();
    } else if (kind == mode::sourceModeName(mode::SourceMode::ApiKey)) {
        song.likeSource = db::TrackRepository::LikeSource::Provider;
    }
    return song;
}

void DownloadLibraryIndex::run()
{
    if (m_reading) {
        m_again = true;
        return;
    }
    const QList<DownloadItem> songs = songsByFile(m_downloads.finishedSongs());
    QByteArray signature = signatureOf(songs);
    if (signature == m_written) {
        return;
    }

    // Only files whose rows do not know their length are read.
    const QHash<QString, double> known = m_tracks.downloadTrackDurations();
    QList<db::TrackRepository::DownloadedTrack> tracks;
    tracks.reserve(songs.size());
    QList<qsizetype> unread;
    for (const DownloadItem& item : songs) {
        db::TrackRepository::DownloadedTrack song = downloadedFor(item, 0.0);
        const double seconds = known.value(catalogSourceKey(song.track), 0.0);
        if (seconds > 0.0) {
            song.track = trackFor(item, seconds);
        } else {
            unread.append(tracks.size());
        }
        tracks.append(std::move(song));
    }
    if (unread.isEmpty()) {
        write(tracks, signature);
        return;
    }

    m_reading = true;
    platform::ITagReader& tags = m_tags;
    m_worker.start([this, &tags, songs, tracks = std::move(tracks), unread = std::move(unread),
                    signature = std::move(signature)]() mutable {
        for (const qsizetype index : std::as_const(unread)) {
            const DownloadItem& item = songs.at(index);
            // A file that cannot be read is listed without a length.
            const Result<platform::TrackTags> read = tags.read(item.filePath);
            if (read && read.value().durationSeconds > 0.0) {
                tracks[index].track = trackFor(item, read.value().durationSeconds);
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, tracks = std::move(tracks), signature = std::move(signature)]() {
                m_reading = false;
                write(tracks, signature);
                if (std::exchange(m_again, false)) {
                    schedule(0);
                }
            },
            Qt::QueuedConnection);
    });
}

void DownloadLibraryIndex::write(const QList<db::TrackRepository::DownloadedTrack>& songs,
                                 const QByteArray& signature)
{
    std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        // User data is being cleaned up: tried again after it.
        schedule(RetryMs);
        return;
    }
    const Result<void> written = m_tracks.syncDownloadTracks(songs);
    if (!written) {
        schedule(RetryMs);
        return;
    }
    m_written = signature;
    emit indexed();
}

} // namespace lmp::core::downloads
