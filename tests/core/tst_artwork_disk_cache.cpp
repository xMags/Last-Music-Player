#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <optional>

#include "core/remote/ArtworkDiskCache.h"

using lmp::core::remote::ArtworkDiskCache;

namespace {

[[nodiscard]] std::optional<QByteArray> readNow(ArtworkDiskCache& disk, const QString& key)
{
    return disk.read(key).result();
}

[[nodiscard]] int keptCount(const QString& folder)
{
    int count = 0;
    for (const QString& scope : QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        // Pictures only: a file being written has a temporary name with a dot.
        const QStringList names = QDir(folder + QLatin1Char('/') + scope + QStringLiteral("/kept")).entryList(QDir::Files);
        count += static_cast<int>(std::count_if(names.cbegin(), names.cend(),
                                                [](const QString& name) { return !name.contains(QLatin1Char('.')); }));
    }
    return count;
}

} // namespace

// Pictures on disk: cached and read back, kept for good and released, scopes
// apart, and the cache trimmed to its budget, oldest use first.
class ArtworkDiskCacheTest : public QObject {
    Q_OBJECT

private slots:
    void keysAreScopedAndSafe();
    void storedPicturesAreReadBack();
    void keptPicturesStayUntilReleased();
    void releaseAllExceptFreesWhatNothingNeeds();
    void theCacheIsTrimmedToItsBudget();
};

void ArtworkDiskCacheTest::keysAreScopedAndSafe()
{
    const QString a = ArtworkDiskCache::fileKey(QStringLiteral("account|a"), QStringLiteral("artwork|x"));
    const QString b = ArtworkDiskCache::fileKey(QStringLiteral("account|b"), QStringLiteral("artwork|x"));
    QCOMPARE(a.size(), 129);
    QVERIFY(a != b);
    QCOMPARE(a.mid(65), b.mid(65)); // the same picture, in two scopes
    QVERIFY(ArtworkDiskCache::fileKey(QString(), QStringLiteral("artwork|x")).isEmpty());
    QTemporaryDir folder;
    ArtworkDiskCache disk(folder.path());
    // Anything that is not a key is never a path.
    QVERIFY(!readNow(disk, QStringLiteral("../../etc")).has_value());
}

void ArtworkDiskCacheTest::storedPicturesAreReadBack()
{
    QTemporaryDir folder;
    ArtworkDiskCache disk(folder.path());
    const QString key = ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|1"));
    QVERIFY(!readNow(disk, key).has_value());
    disk.store(key, QByteArray("picture"));
    QTRY_COMPARE(readNow(disk, key).value_or(QByteArray()), QByteArray("picture"));
    // Another scope does not see it.
    QVERIFY(!readNow(disk, ArtworkDiskCache::fileKey(QStringLiteral("account|a"), QStringLiteral("artwork|1"))));
}

void ArtworkDiskCacheTest::keptPicturesStayUntilReleased()
{
    QTemporaryDir folder;
    ArtworkDiskCache disk(folder.path());
    const QString key = ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|2"));
    QVERIFY(!disk.keepIfCached(key).result());
    disk.store(key, QByteArray("cover"));
    QTRY_VERIFY(readNow(disk, key).has_value());
    QVERIFY(disk.keepIfCached(key).result());
    QCOMPARE(keptCount(folder.path()), 1);
    QCOMPARE(readNow(disk, key).value_or(QByteArray()), QByteArray("cover"));
    disk.release(key);
    QTRY_COMPARE(keptCount(folder.path()), 0);
    // Released: still cached.
    QCOMPARE(readNow(disk, key).value_or(QByteArray()), QByteArray("cover"));
    disk.keep(key, QByteArray("fresh"));
    QTRY_COMPARE(readNow(disk, key).value_or(QByteArray()), QByteArray("fresh"));
    QCOMPARE(keptCount(folder.path()), 1);
}

void ArtworkDiskCacheTest::releaseAllExceptFreesWhatNothingNeeds()
{
    QTemporaryDir folder;
    ArtworkDiskCache disk(folder.path());
    const QString scope = QStringLiteral("open");
    const QString needed = ArtworkDiskCache::fileKey(scope, QStringLiteral("artwork|a"));
    const QString removed = ArtworkDiskCache::fileKey(scope, QStringLiteral("artwork|b"));
    const QString otherScope = ArtworkDiskCache::fileKey(QStringLiteral("account|z"), QStringLiteral("artwork|b"));
    disk.keep(needed, QByteArray("a"));
    disk.keep(removed, QByteArray("b"));
    disk.keep(otherScope, QByteArray("c"));
    QTRY_COMPARE(keptCount(folder.path()), 3);
    disk.releaseAllExcept(scope, {needed});
    // Only this scope's unneeded picture goes back; the other scope's stays kept.
    QTRY_COMPARE(keptCount(folder.path()), 2);
    QCOMPARE(readNow(disk, removed).value_or(QByteArray()), QByteArray("b"));
}

void ArtworkDiskCacheTest::theCacheIsTrimmedToItsBudget()
{
    QTemporaryDir folder;
    ArtworkDiskCache disk(folder.path(), 10);
    const QString kept = ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|kept"));
    disk.keep(kept, QByteArray("kept-picture-longer-than-the-budget"));
    for (int i = 0; i < 5; ++i) {
        disk.store(ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|%1").arg(i)),
                   QByteArray("0123"));
    }
    const QString newest = ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|4"));
    QTRY_VERIFY(readNow(disk, newest).has_value());
    // At most 8 bytes of cache stay (80% of the budget); kept pictures never go.
    QTRY_VERIFY(!readNow(disk, ArtworkDiskCache::fileKey(QStringLiteral("open"), QStringLiteral("artwork|0"))));
    QVERIFY(readNow(disk, kept).has_value());
}

QTEST_GUILESS_MAIN(ArtworkDiskCacheTest)
#include "tst_artwork_disk_cache.moc"
