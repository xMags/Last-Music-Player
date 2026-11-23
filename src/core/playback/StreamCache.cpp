#include "core/playback/StreamCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <memory>
#include <vector>

namespace lmp::core {

QString streamExtensionForContentType(const QString& contentType)
{
    const QString type = contentType.toLower();
    if (type.contains(QLatin1StringView("mpeg")) || type.contains(QLatin1StringView("mp3"))) {
        return QStringLiteral(".mp3");
    }
    if (type.contains(QLatin1StringView("mp4")) || type.contains(QLatin1StringView("m4a"))
        || type.contains(QLatin1StringView("aac"))) {
        return QStringLiteral(".m4a");
    }
    if (type.contains(QLatin1StringView("webm"))) {
        return QStringLiteral(".webm");
    }
    if (type.contains(QLatin1StringView("ogg")) || type.contains(QLatin1StringView("opus"))) {
        return QStringLiteral(".ogg");
    }
    if (type.contains(QLatin1StringView("wav"))) {
        return QStringLiteral(".wav");
    }
    if (type.contains(QLatin1StringView("flac"))) {
        return QStringLiteral(".flac");
    }
    return QStringLiteral(".bin");
}

StreamCache::StreamCache(QString folder, UserDataOperationGate& gate, IStreamDownloader& downloader, QObject* parent)
    : QObject(parent)
    , m_folder(std::move(folder))
    , m_gate(gate)
    , m_downloader(downloader)
{
}

QString StreamCache::hashKey(const QString& sourceKey)
{
    // The earlier versions' FNV-1a over UTF-16, with their offset basis
    // (one digit short of the usual value), so files from them are found.
    quint64 hash = 1469598103934665603ULL;
    for (const QChar character : sourceKey) {
        const char16_t unit = character.unicode();
        for (int shift = 0; shift < 16; shift += 8) {
            hash ^= static_cast<quint8>((unit >> shift) & 0xFF);
            hash *= 1099511628211ULL;
        }
    }
    return QStringLiteral("%1").arg(hash, 16, 16, QLatin1Char('0'));
}

QString StreamCache::findOnDisk(const QString& hash) const
{
    QString newestPath;
    QDateTime newest;
    const QFileInfoList files = QDir(m_folder).entryInfoList({hash + QStringLiteral(".*")}, QDir::Files);
    for (const QFileInfo& file : files) {
        if (file.suffix() == QLatin1StringView("part")) {
            continue;
        }
        const QDateTime written = file.lastModified();
        if (newestPath.isEmpty() || written > newest) {
            newestPath = file.absoluteFilePath();
            newest = written;
        }
    }
    return newestPath;
}

QString StreamCache::readyPath(const QString& sourceKey)
{
    if (sourceKey.isEmpty()) {
        return {};
    }
    if (const auto found = m_entries.constFind(sourceKey); found != m_entries.cend()) {
        if (found->status == Status::InFlight) {
            return {};
        }
        if (found->status == Status::Ready) {
            if (QFileInfo(found->path).isFile()) {
                return found->path;
            }
            m_entries.remove(sourceKey);
        }
    }
    const QString onDisk = findOnDisk(hashKey(sourceKey));
    if (onDisk.isEmpty()) {
        return {};
    }
    m_entries.insert(sourceKey, Entry{Status::Ready, onDisk, 0});
    return onDisk;
}

void StreamCache::prefetch(const QString& sourceKey, const QUrl& streamUrl)
{
    if (sourceKey.isEmpty() || !streamUrl.isValid() || streamUrl.isEmpty()) {
        return;
    }
    if (const auto found = m_entries.constFind(sourceKey);
        found != m_entries.cend() && found->status != Status::Failed) {
        return;
    }
    if (const QString existing = findOnDisk(hashKey(sourceKey)); !existing.isEmpty()) {
        m_entries.insert(sourceKey, Entry{Status::Ready, existing, 0});
        return;
    }
    if (m_active >= MaxInFlight) {
        return;
    }
    std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return;
    }
    if (!QDir().mkpath(m_folder)) {
        return;
    }
    const std::uint64_t generation = m_generation;
    const std::uint64_t attempt = m_nextAttempt++;
    const QString partPath = QDir(m_folder).filePath(hashKey(sourceKey) + QLatin1Char('.') + QString::number(generation)
                                                     + QLatin1Char('.') + QString::number(attempt)
                                                     + QStringLiteral(".part"));
    ++m_active;
    m_entries.insert(sourceKey, Entry{Status::InFlight, {}, attempt});
    // The lease is held until the transfer returns, whatever happened meanwhile.
    auto held = std::make_shared<UserDataOperationGate::Lease>(std::move(*lease));
    m_downloader.download(streamUrl, partPath)
        .then(this, [this, sourceKey, generation, attempt, partPath, held](const Result<QString>& extension) {
            --m_active;
            finish(sourceKey, generation, attempt, partPath, extension);
            *held = UserDataOperationGate::Lease{};
        });
}

void StreamCache::finish(const QString& sourceKey, std::uint64_t generation, std::uint64_t attempt,
                         const QString& partPath, const Result<QString>& extension)
{
    const auto current = [this, &sourceKey, generation, attempt]() {
        const auto found = m_entries.constFind(sourceKey);
        return generation == m_generation && found != m_entries.cend() && found->status == Status::InFlight
            && found->attempt == attempt;
    };
    if (!extension || extension.value().isEmpty() || !current()) {
        QFile::remove(partPath);
        if (current()) {
            m_entries.insert(sourceKey, Entry{Status::Failed, {}, attempt});
        }
        return;
    }
    QString finalPath = partPath;
    finalPath.chop(5); // ".part"
    finalPath += extension.value();
    QFile::remove(finalPath);
    if (!QFile::rename(partPath, finalPath)) {
        QFile::remove(partPath);
        m_entries.insert(sourceKey, Entry{Status::Failed, {}, attempt});
        return;
    }
    m_entries.insert(sourceKey, Entry{Status::Ready, finalPath, attempt});
    trimToCap();
}

void StreamCache::trimToCap()
{
    struct FileFacts {
        QString path;
        quint64 size = 0;
        QDateTime written;
    };
    std::vector<FileFacts> files;
    quint64 total = 0;
    for (const QFileInfo& file : QDir(m_folder).entryInfoList(QDir::Files)) {
        if (file.suffix() == QLatin1StringView("part")) {
            continue;
        }
        files.push_back({file.absoluteFilePath(), static_cast<quint64>(file.size()), file.lastModified()});
        total += static_cast<quint64>(file.size());
    }
    if (static_cast<qsizetype>(files.size()) <= MaxFiles && total <= MaxBytes) {
        return;
    }
    std::stable_sort(files.begin(), files.end(),
                     [](const FileFacts& a, const FileFacts& b) { return a.written < b.written; });
    auto count = static_cast<qsizetype>(files.size());
    for (const FileFacts& file : files) {
        if (count <= MaxFiles && total <= MaxBytes) {
            break;
        }
        if (!QFile::remove(file.path)) {
            continue;
        }
        --count;
        total -= std::min(file.size, total);
        for (auto entry = m_entries.begin(); entry != m_entries.end();) {
            entry = entry->path == file.path ? m_entries.erase(entry) : std::next(entry);
        }
    }
}

void StreamCache::pruneOnStartup()
{
    for (const QFileInfo& file : QDir(m_folder).entryInfoList({QStringLiteral("*.part")}, QDir::Files)) {
        QFile::remove(file.absoluteFilePath());
    }
    trimToCap();
}

void StreamCache::invalidateInFlight()
{
    ++m_generation;
    m_entries.clear();
}

bool StreamCache::clear()
{
    if (m_active != 0) {
        return false;
    }
    ++m_generation;
    m_entries.clear();
    QDir folder(m_folder);
    if (folder.exists() && !folder.removeRecursively()) {
        return false;
    }
    return !QFileInfo::exists(m_folder);
}

StreamCacheUsage StreamCache::usage() const
{
    StreamCacheUsage result;
    for (const QFileInfo& file : QDir(m_folder).entryInfoList(QDir::Files)) {
        if (file.suffix() == QLatin1StringView("part")) {
            continue;
        }
        result.bytes += static_cast<quint64>(file.size());
        ++result.files;
    }
    return result;
}

} // namespace lmp::core
