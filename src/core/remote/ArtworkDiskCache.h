#pragma once

#include <QByteArray>
#include <QFuture>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <optional>

namespace lmp::core::remote {

// Pictures kept on disk between runs, so covers and artist photos show
// without the network: the pictures the app has shown ("cache", trimmed to a
// budget, least recently used first) and the pictures kept for good while
// something needs them, such as a downloaded song's cover ("kept", never
// trimmed). Each scope (an account, or the open web) has a folder of its
// own, so one account's pictures are never answered for another:
//
//   <folder>/<scope hash>/cache/<picture hash>
//   <folder>/<scope hash>/kept/<picture hash>
//
// A file key is "<scope hash>/<picture hash>" (fileKey()). Files hold the
// picture's bytes as fetched; readers tell the format from the bytes.
//
// Threading: every call may come from the GUI thread; the file work runs on
// this object's own worker thread, one task at a time in the order asked,
// and the futures finish there (continue them
// with a context object). The destructor waits for work still running, so
// this must outlive nothing that posts to it after that.
class ArtworkDiskCache {
public:
    static constexpr qint64 DefaultBudgetBytes = 512LL * 1024 * 1024;

    explicit ArtworkDiskCache(QString folder, qint64 budgetBytes = DefaultBudgetBytes);
    ~ArtworkDiskCache();

    ArtworkDiskCache(const ArtworkDiskCache&) = delete;
    ArtworkDiskCache& operator=(const ArtworkDiskCache&) = delete;

    // The key `pictureKey` is stored under for `scope`; empty when either is.
    [[nodiscard]] static QString fileKey(const QString& scope, const QString& pictureKey);

    // The picture's bytes, kept or cached; nothing when neither has it. A
    // cached picture counts as used now (it is trimmed last).
    [[nodiscard]] QFuture<std::optional<QByteArray>> read(const QString& fileKey);
    // Caches a picture that was fetched.
    void store(const QString& fileKey, QByteArray bytes);
    // Keeps a picture for good.
    void keep(const QString& fileKey, QByteArray bytes);
    // Keeps a picture for good from what is on disk: true when it is kept now.
    [[nodiscard]] QFuture<bool> keepIfCached(const QString& fileKey);
    // A kept picture nothing needs any more goes back to the cache (and is
    // trimmed in its turn).
    void release(const QString& fileKey);
    // Releases every kept picture of `scope` except `fileKeys`: what a
    // removed download left behind while the app was closed.
    void releaseAllExcept(const QString& scope, const QSet<QString>& fileKeys);

    [[nodiscard]] QString folder() const { return m_folder; }

private:
    [[nodiscard]] QString pathFor(const QString& fileKey, bool kept) const;
    [[nodiscard]] static bool writeFile(const QString& path, const QByteArray& bytes);
    void releaseNow(const QString& fileKey);
    void addCached(qint64 bytes);
    void trim();

    QString m_folder;
    qint64 m_budget;
    QMutex m_lock;
    // The bytes in every cache folder; counted on the first change.
    std::optional<qint64> m_cachedBytes;
    QThreadPool m_pool;
};

} // namespace lmp::core::remote
