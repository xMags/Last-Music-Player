// The API key mode stream cache. The first two cases are ported from the
// earlier versions' NativeAccountSyncTests (stale attempts never publish,
// retired transfers still count toward the cap); the rest cover the file
// rules the earlier StreamCache.cpp applied.

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPromise>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <deque>
#include <memory>

#include "core/library/UserDataOperationGate.h"
#include "core/playback/StreamCache.h"

using lmp::core::Result;
using lmp::core::StreamCache;
using lmp::core::UserDataOperationGate;

namespace {

// Holds each transfer until the test lets it finish, then writes its bytes.
class FakeDownloader final : public lmp::core::IStreamDownloader {
public:
    struct Pending {
        QString partPath;
        QUrl url;
        std::shared_ptr<QPromise<Result<QString>>> promise;
    };

    QFuture<Result<QString>> download(const QUrl& streamUrl, const QString& partPath) override
    {
        auto promise = std::make_shared<QPromise<Result<QString>>>();
        promise->start();
        pending.push_back({partPath, streamUrl, promise});
        kept.push_back(promise);
        return promise->future();
    }

    // Writes `bytes` to the oldest waiting transfer's file and finishes it.
    bool finishOldest(const QByteArray& bytes, const QString& extension = QStringLiteral(".mp3"))
    {
        if (pending.empty()) {
            return false;
        }
        Pending next = pending.front();
        pending.pop_front();
        QFile file(next.partPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            return false;
        }
        file.close();
        next.promise->addResult(Result<QString>(extension));
        next.promise->finish();
        return true;
    }

    std::deque<Pending> pending;
    std::vector<std::shared_ptr<QPromise<Result<QString>>>> kept;
};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
}

} // namespace

class StreamCacheTest : public QObject {
    Q_OBJECT

private slots:
    void staleAttemptNeverPublishes();
    void retiredTransfersCountTowardTheCap();
    void hashAndNamesFollowTheEarlierVersions();
    void earlierFilesAreFoundAndPartsPruned();
    void folderIsTrimmedOldestFirst();
    void closedGateStartsNothing();
};

void StreamCacheTest::staleAttemptNeverPublishes()
{
    QTemporaryDir root;
    const QString folder = root.filePath(QStringLiteral("stream-cache"));
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(folder, gate, downloader);
    const QString key = QStringLiteral("ApiKey:7\nhttps://media.example.test/track");
    cache.prefetch(key, QUrl(QStringLiteral("https://stream.example.test/stale")));
    QCOMPARE(downloader.pending.size(), size_t(1));

    cache.invalidateInFlight();
    cache.prefetch(key, QUrl(QStringLiteral("https://stream.example.test/current")));
    QCOMPARE(downloader.pending.size(), size_t(2));
    // The replacement finishes first.
    FakeDownloader::Pending stale = downloader.pending.front();
    downloader.pending.pop_front();
    QVERIFY(downloader.finishOldest("current"));
    QString ready;
    QVERIFY(QTest::qWaitFor([&]() { return !(ready = cache.readyPath(key)).isEmpty(); }, 2000));
    QCOMPARE(readFile(ready), QByteArray("current"));

    // Cleaning up while the stale transfer still runs is refused.
    QVERIFY(gate.closeAdmissions());
    QVERIFY(!cache.clear());
    downloader.pending.push_front(stale);
    QVERIFY(downloader.finishOldest("stale"));
    QVERIFY(QTest::qWaitFor([&]() { return cache.activeTransfers() == 0; }, 2000));
    QVERIFY(gate.waitForIdle(std::chrono::seconds(2)));

    QCOMPARE(cache.readyPath(key), ready);
    QCOMPARE(readFile(ready), QByteArray("current"));
    QVERIFY(cache.clear());
    gate.reopen();
    QVERIFY(!QFileInfo::exists(folder));
}

void StreamCacheTest::retiredTransfersCountTowardTheCap()
{
    QTemporaryDir root;
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(root.filePath(QStringLiteral("stream-cache")), gate, downloader);
    cache.prefetch(QStringLiteral("ApiKey:p:7\nhttps://media.example.test/first"), QUrl(QStringLiteral("https://s.example.test/1")));
    cache.prefetch(QStringLiteral("ApiKey:p:7\nhttps://media.example.test/second"), QUrl(QStringLiteral("https://s.example.test/2")));
    QCOMPARE(downloader.pending.size(), size_t(2));

    cache.invalidateInFlight();
    const QString third = QStringLiteral("ApiKey:p:8\nhttps://media.example.test/third");
    cache.prefetch(third, QUrl(QStringLiteral("https://s.example.test/3")));
    // Both slots are still taken by the retired transfers.
    QCOMPARE(downloader.pending.size(), size_t(2));

    QVERIFY(downloader.finishOldest("first"));
    QVERIFY(QTest::qWaitFor([&]() { return cache.activeTransfers() == 1; }, 2000));
    cache.prefetch(third, QUrl(QStringLiteral("https://s.example.test/3")));
    QCOMPARE(downloader.pending.size(), size_t(2));
    QCOMPARE(downloader.pending.back().url, QUrl(QStringLiteral("https://s.example.test/3")));

    QVERIFY(downloader.finishOldest("second"));
    QVERIFY(downloader.finishOldest("third"));
    QVERIFY(QTest::qWaitFor([&]() { return cache.activeTransfers() == 0; }, 2000));
    QVERIFY(gate.closeAdmissions());
    QVERIFY(gate.waitForIdle(std::chrono::seconds(2)));
    QVERIFY(cache.clear());
    gate.reopen();
}

void StreamCacheTest::hashAndNamesFollowTheEarlierVersions()
{
    QCOMPARE(StreamCache::hashKey(QString()), QStringLiteral("14650fb0739d0383"));
    QCOMPARE(lmp::core::streamExtensionForContentType(QStringLiteral("audio/MPEG")), QStringLiteral(".mp3"));
    QCOMPARE(lmp::core::streamExtensionForContentType(QStringLiteral("audio/mp4; codecs=mp4a")), QStringLiteral(".m4a"));
    QCOMPARE(lmp::core::streamExtensionForContentType(QStringLiteral("audio/webm")), QStringLiteral(".webm"));
    QCOMPARE(lmp::core::streamExtensionForContentType(QStringLiteral("audio/ogg; codecs=opus")), QStringLiteral(".ogg"));
    QCOMPARE(lmp::core::streamExtensionForContentType(QStringLiteral("application/octet-stream")), QStringLiteral(".bin"));

    QTemporaryDir root;
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(root.filePath(QStringLiteral("stream-cache")), gate, downloader);
    const QString key = QStringLiteral("ApiKey:p:1\nhttps://media.example.test/a");
    cache.prefetch(key, QUrl(QStringLiteral("https://s.example.test/a")));
    QVERIFY(downloader.pending.front().partPath.endsWith(StreamCache::hashKey(key) + QStringLiteral(".0.1.part")));
    QVERIFY(cache.readyPath(key).isEmpty());
    QVERIFY(downloader.finishOldest("bytes", QStringLiteral(".m4a")));
    QString ready;
    QVERIFY(QTest::qWaitFor([&]() { return !(ready = cache.readyPath(key)).isEmpty(); }, 2000));
    QVERIFY(ready.endsWith(StreamCache::hashKey(key) + QStringLiteral(".0.1.m4a")));
    QCOMPARE(cache.usage().files, 1);
    QCOMPARE(cache.usage().bytes, quint64(5));
}

void StreamCacheTest::earlierFilesAreFoundAndPartsPruned()
{
    QTemporaryDir root;
    const QString folder = root.filePath(QStringLiteral("stream-cache"));
    QVERIFY(QDir().mkpath(folder));
    const QString key = QStringLiteral("ApiKey:p:1\nhttps://media.example.test/a");
    const QString earlier = QDir(folder).filePath(StreamCache::hashKey(key) + QStringLiteral(".3.9.mp3"));
    writeFile(earlier, "old");
    const QString leftover = QDir(folder).filePath(StreamCache::hashKey(key) + QStringLiteral(".4.1.part"));
    writeFile(leftover, "half");
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(folder, gate, downloader);
    QCOMPARE(QFileInfo(cache.readyPath(key)).fileName(), QFileInfo(earlier).fileName());
    // Already on disk: nothing is fetched.
    cache.prefetch(key, QUrl(QStringLiteral("https://s.example.test/a")));
    QVERIFY(downloader.pending.empty());
    cache.pruneOnStartup();
    QVERIFY(!QFileInfo::exists(leftover));
    QVERIFY(QFileInfo::exists(earlier));
}

void StreamCacheTest::folderIsTrimmedOldestFirst()
{
    QTemporaryDir root;
    const QString folder = root.filePath(QStringLiteral("stream-cache"));
    QVERIFY(QDir().mkpath(folder));
    // 25 files, the first the oldest.
    const QDateTime start = QDateTime::currentDateTimeUtc().addDays(-1);
    for (int index = 0; index < 25; ++index) {
        const QString path = QDir(folder).filePath(QStringLiteral("%1.0.1.mp3").arg(index, 16, 10, QLatin1Char('0')));
        writeFile(path, "x");
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(start.addSecs(index), QFileDevice::FileModificationTime));
    }
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(folder, gate, downloader);
    cache.pruneOnStartup();
    QCOMPARE(cache.usage().files, StreamCache::MaxFiles);
    QVERIFY(!QFileInfo::exists(QDir(folder).filePath(QStringLiteral("%1.0.1.mp3").arg(0, 16, 10, QLatin1Char('0')))));
    QVERIFY(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("%1.0.1.mp3").arg(1, 16, 10, QLatin1Char('0')))));
}

void StreamCacheTest::closedGateStartsNothing()
{
    QTemporaryDir root;
    UserDataOperationGate gate;
    FakeDownloader downloader;
    StreamCache cache(root.filePath(QStringLiteral("stream-cache")), gate, downloader);
    QVERIFY(gate.closeAdmissions());
    cache.prefetch(QStringLiteral("ApiKey:p:1\nhttps://media.example.test/a"), QUrl(QStringLiteral("https://s.example.test/a")));
    QVERIFY(downloader.pending.empty());
    gate.reopen();
    // Empty keys and addresses start nothing either.
    cache.prefetch(QString(), QUrl(QStringLiteral("https://s.example.test/a")));
    cache.prefetch(QStringLiteral("k"), QUrl());
    QVERIFY(downloader.pending.empty());
}

QTEST_GUILESS_MAIN(StreamCacheTest)
#include "tst_stream_cache.moc"
