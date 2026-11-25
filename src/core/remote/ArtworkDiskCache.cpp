#include "core/remote/ArtworkDiskCache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSaveFile>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>
#include <vector>

namespace lmp::core::remote {

namespace {

constexpr auto kCacheFolder = QLatin1StringView("cache");
constexpr auto kKeptFolder = QLatin1StringView("kept");

[[nodiscard]] QString hashOf(const QString& text)
{
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// A file key is two hex hashes joined by "/"; nothing else is ever a path.
[[nodiscard]] bool validKey(const QString& fileKey)
{
    const qsizetype slash = fileKey.indexOf(QLatin1Char('/'));
    if (slash != 64 || fileKey.size() != 129) {
        return false;
    }
    return std::all_of(fileKey.cbegin(), fileKey.cend(), [](QChar c) {
        return c == QLatin1Char('/') || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
               || (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
    });
}

// Two writers may make the same folder at once; the one that loses the
// race still has its folder.
[[nodiscard]] bool makeFolder(const QString& path)
{
    return QDir().mkpath(path) || QFileInfo(path).isDir();
}

// Marks a cached picture as used now: the trim goes by this time. A file's
// times can only be set through a handle that may write.
void touch(const QString& path)
{
    QFile file(path);
    if (file.open(QIODevice::ReadWrite)) {
        (void)file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
    }
}

} // namespace

ArtworkDiskCache::ArtworkDiskCache(QString folder, qint64 budgetBytes)
    : m_folder(std::move(folder))
    , m_budget(budgetBytes)
{
    // One worker, so the file work runs in the order it was asked for: a
    // keep, a release or a trim never races another write of the same
    // folders (on Windows a replace can fail while another thread works in
    // the folder). The files are small; one at a time keeps up.
    m_pool.setMaxThreadCount(1);
}

ArtworkDiskCache::~ArtworkDiskCache()
{
    m_pool.waitForDone();
}

QString ArtworkDiskCache::fileKey(const QString& scope, const QString& pictureKey)
{
    if (scope.isEmpty() || pictureKey.isEmpty()) {
        return {};
    }
    return hashOf(scope) + QLatin1Char('/') + hashOf(pictureKey);
}

QString ArtworkDiskCache::pathFor(const QString& fileKey, bool kept) const
{
    const QString scope = fileKey.left(64);
    const QString picture = fileKey.mid(65);
    return m_folder + QLatin1Char('/') + scope + QLatin1Char('/') + (kept ? kKeptFolder : kCacheFolder)
           + QLatin1Char('/') + picture;
}

bool ArtworkDiskCache::writeFile(const QString& path, const QByteArray& bytes)
{
    if (!makeFolder(QFileInfo(path).absolutePath())) {
        return false;
    }
    // Written whole or not at all: a half picture is never read back.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QFuture<std::optional<QByteArray>> ArtworkDiskCache::read(const QString& fileKey)
{
    if (!validKey(fileKey)) {
        return QtFuture::makeReadyValueFuture(std::optional<QByteArray>());
    }
    return QtConcurrent::run(&m_pool, [this, fileKey]() -> std::optional<QByteArray> {
        for (const bool kept : {true, false}) {
            QFile file(pathFor(fileKey, kept));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            QByteArray bytes = file.readAll();
            if (bytes.isEmpty()) {
                continue;
            }
            if (!kept) {
                // Used now: trimmed last.
                file.close();
                touch(pathFor(fileKey, false));
            }
            return bytes;
        }
        return std::nullopt;
    });
}

void ArtworkDiskCache::store(const QString& fileKey, QByteArray bytes)
{
    if (!validKey(fileKey) || bytes.isEmpty()) {
        return;
    }
    (void)QtConcurrent::run(&m_pool, [this, fileKey, bytes = std::move(bytes)]() {
        if (QFileInfo::exists(pathFor(fileKey, true))) {
            return; // kept already
        }
        const QString path = pathFor(fileKey, false);
        const qint64 before = QFileInfo(path).size();
        if (writeFile(path, bytes)) {
            addCached(bytes.size() - before);
        }
    });
}

void ArtworkDiskCache::keep(const QString& fileKey, QByteArray bytes)
{
    if (!validKey(fileKey) || bytes.isEmpty()) {
        return;
    }
    (void)QtConcurrent::run(&m_pool, [this, fileKey, bytes = std::move(bytes)]() {
        if (!writeFile(pathFor(fileKey, true), bytes)) {
            return;
        }
        // The cached copy is not needed beside the kept one.
        const QString cached = pathFor(fileKey, false);
        const qint64 size = QFileInfo(cached).size();
        if (size > 0 && QFile::remove(cached)) {
            addCached(-size);
        }
    });
}

QFuture<bool> ArtworkDiskCache::keepIfCached(const QString& fileKey)
{
    if (!validKey(fileKey)) {
        return QtFuture::makeReadyValueFuture(false);
    }
    return QtConcurrent::run(&m_pool, [this, fileKey]() {
        const QString kept = pathFor(fileKey, true);
        if (QFileInfo(kept).size() > 0) {
            return true;
        }
        const QString cached = pathFor(fileKey, false);
        const qint64 size = QFileInfo(cached).size();
        if (size <= 0 || !makeFolder(QFileInfo(kept).absolutePath()) || !QFile::rename(cached, kept)) {
            return false;
        }
        addCached(-size);
        return true;
    });
}

void ArtworkDiskCache::release(const QString& fileKey)
{
    if (!validKey(fileKey)) {
        return;
    }
    (void)QtConcurrent::run(&m_pool, [this, fileKey]() { releaseNow(fileKey); });
}

void ArtworkDiskCache::releaseNow(const QString& fileKey)
{
    const QString kept = pathFor(fileKey, true);
    const qint64 size = QFileInfo(kept).size();
    if (size <= 0) {
        return;
    }
    const QString cached = pathFor(fileKey, false);
    QFile::remove(cached);
    if (makeFolder(QFileInfo(cached).absolutePath()) && QFile::rename(kept, cached)) {
        // Released now, so it counts as used now.
        touch(cached);
        addCached(size);
    } else {
        QFile::remove(kept);
    }
}

void ArtworkDiskCache::releaseAllExcept(const QString& scope, const QSet<QString>& fileKeys)
{
    if (scope.isEmpty()) {
        return;
    }
    const QString scopeHash = hashOf(scope);
    (void)QtConcurrent::run(&m_pool, [this, scopeHash, fileKeys]() {
        const QDir kept(m_folder + QLatin1Char('/') + scopeHash + QLatin1Char('/') + kKeptFolder);
        for (const QString& name : kept.entryList(QDir::Files)) {
            const QString key = scopeHash + QLatin1Char('/') + name;
            if (validKey(key) && !fileKeys.contains(key)) {
                releaseNow(key);
            }
        }
    });
}

void ArtworkDiskCache::addCached(qint64 bytes)
{
    bool over = false;
    {
        const QMutexLocker locker(&m_lock);
        if (!m_cachedBytes) {
            // Counted once, from the files themselves (this change included).
            qint64 total = 0;
            QDirIterator files(m_folder, QDir::Files, QDirIterator::Subdirectories);
            while (files.hasNext()) {
                const QFileInfo info = files.nextFileInfo();
                if (info.dir().dirName() == kCacheFolder) {
                    total += info.size();
                }
            }
            m_cachedBytes = total;
        } else {
            m_cachedBytes = std::max<qint64>(0, *m_cachedBytes + bytes);
        }
        over = *m_cachedBytes > m_budget;
    }
    if (over) {
        trim();
    }
}

void ArtworkDiskCache::trim()
{
    const QMutexLocker locker(&m_lock);
    std::vector<QFileInfo> cached;
    QDirIterator files(m_folder, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        QFileInfo info = files.nextFileInfo();
        if (info.dir().dirName() == kCacheFolder) {
            cached.push_back(std::move(info));
        }
    }
    std::sort(cached.begin(), cached.end(),
              [](const QFileInfo& a, const QFileInfo& b) { return a.lastModified() < b.lastModified(); });
    qint64 total = 0;
    for (const QFileInfo& info : cached) {
        total += info.size();
    }
    // Down to 80% of the budget, so a full cache does not trim on every store.
    const qint64 target = m_budget / 5 * 4;
    for (const QFileInfo& info : cached) {
        if (total <= target) {
            break;
        }
        if (QFile::remove(info.absoluteFilePath())) {
            total -= info.size();
        }
    }
    m_cachedBytes = total;
}

} // namespace lmp::core::remote
