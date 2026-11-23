// The offline download manager over a fake fetcher and fake rules: the
// earlier Downloads.json, resuming a partial file, file names, the two-transfer
// cap, the network and battery rules, pausing, and items of another scope.

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QTemporaryDir>
#include <QTest>

#include <deque>
#include <memory>
#include <vector>

#include "core/downloads/DownloadManager.h"
#include "core/settings/SettingKeys.h"
#include "core/settings/SettingsStore.h"

using lmp::core::DownloadItemState;
using lmp::core::DownloadManager;
using lmp::core::DownloadTrackRequest;
using lmp::core::IDownloadFetcher;
using lmp::core::Result;

namespace {

class FakeFetcher final : public IDownloadFetcher {
public:
    struct Pending {
        QUrl url;
        QString path;
        std::uint64_t offset = 0;
        Stop stop;
        std::shared_ptr<QPromise<Result<Outcome>>> promise;
    };

    QFuture<Result<Outcome>> fetch(const QUrl& url, const QString& filePath, std::uint64_t offset, Stop stop,
                                   Progress) override
    {
        auto promise = std::make_shared<QPromise<Result<Outcome>>>();
        promise->start();
        pending.push_back({url, filePath, offset, std::move(stop), promise});
        kept.push_back(promise);
        return promise->future();
    }

    // Writes `bytes` (appended when `ranged`) and finishes the oldest transfer.
    void finish(const QByteArray& bytes, bool ranged = false, const QString& type = QStringLiteral("audio/mpeg"))
    {
        QVERIFY(!pending.empty());
        Pending next = pending.front();
        pending.pop_front();
        QFile file(next.path);
        QVERIFY(file.open(QIODevice::WriteOnly | (ranged && next.offset > 0 ? QIODevice::Append : QIODevice::Truncate)));
        file.write(bytes);
        file.close();
        next.promise->addResult(Result<Outcome>(Outcome{false, ranged && next.offset > 0, type}));
        next.promise->finish();
    }

    void stopOldest()
    {
        QVERIFY(!pending.empty());
        Pending next = pending.front();
        pending.pop_front();
        next.promise->addResult(Result<Outcome>(Outcome{true, false, {}}));
        next.promise->finish();
    }

    std::deque<Pending> pending;
    std::vector<std::shared_ptr<QPromise<Result<Outcome>>>> kept;
};

struct Fixture {
    Fixture()
        : settings(folder.filePath(QStringLiteral("Settings.json")))
        , manager({.settings = settings,
                   .gate = gate,
                   .fetcher = fetcher,
                   .stateFile = folder.filePath(QStringLiteral("Downloads.json")),
                   .defaultFolder = folder.filePath(QStringLiteral("Offline")),
                   .resolve = [](const lmp::core::DownloadItem& item) {
                       return QtFuture::makeReadyValueFuture(
                           Result<QUrl>(QUrl(QStringLiteral("https://stream.example.test/") + item.title)));
                   },
                   .scopePrefix = [this]() { return scope; },
                   .metered = [this]() { return metered; },
                   .onBattery = [this]() { return battery; }})
    {
    }

    [[nodiscard]] DownloadTrackRequest song(const QString& title) const
    {
        return {scope + title, QStringLiteral("https://music.example.test/") + title, QStringLiteral("tag-b"), title,
                QStringLiteral("Artist"), QStringLiteral("Album"), {}};
    }

    [[nodiscard]] DownloadItemState stateOf(const QString& title) const
    {
        for (const auto& job : manager.snapshot().jobs) {
            for (const auto& item : job.job.items) {
                if (item.title == title) {
                    return item.state;
                }
            }
        }
        return DownloadItemState::Queued;
    }

    QTemporaryDir folder;
    lmp::core::SettingsStore settings;
    lmp::core::UserDataOperationGate gate;
    FakeFetcher fetcher;
    QString scope = QStringLiteral("download|Account:1:1\n");
    bool metered = false;
    bool battery = false;
    DownloadManager manager;
};

} // namespace

class DownloadManagerTest : public QObject {
    Q_OBJECT

private slots:
    void namesAreMadeSafe();
    void downloadsCompleteWithTheEarlierNames();
    void twoTransfersAtOnce();
    void partialFilesResume();
    void rulesHoldTheQueue();
    void itemsOfAnotherScopeFail();
    void earlierStateFileIsRead();
};

void DownloadManagerTest::namesAreMadeSafe()
{
    QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("a<b>c:d\"e/f\\g|h?i*j")), QStringLiteral("a_b_c_d_e_f_g_h_i_j"));
    QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("Song. . ")), QStringLiteral("Song"));
    QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral(" ..")), QStringLiteral("Track"));
    QCOMPARE(DownloadManager::sanitizeFileName(QString(100, QLatin1Char('x'))).size(), 80);
}

void DownloadManagerTest::downloadsCompleteWithTheEarlierNames()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("One"))}));
    // The same song again is not queued twice.
    QVERIFY(!fixture.manager.enqueue(QStringLiteral("Again"), {}, {}, {fixture.song(QStringLiteral("One"))}));
    QCOMPARE(fixture.fetcher.pending.size(), size_t(1));
    QVERIFY(fixture.fetcher.pending.front().path.contains(QStringLiteral(".lmp-part-")));
    fixture.fetcher.finish("audio bytes");
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Completed);
    const QString file = fixture.manager.readyPath(fixture.song(QStringLiteral("One")).stableKey);
    QVERIFY(QFileInfo(file).fileName().startsWith(QStringLiteral("One-")));
    QVERIFY(file.endsWith(QStringLiteral(".mp3")));
    QCOMPARE(QFileInfo(file).fileName().size(), QStringLiteral("One-").size() + 8 + 4);
    QCOMPARE(fixture.manager.snapshot().offlineTracks, 1);
    // The state file holds the earlier shape.
    QFile state(fixture.folder.filePath(QStringLiteral("Downloads.json")));
    QVERIFY(state.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(state.readAll()).object();
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), 1);
    const QJsonObject item = root.value(QStringLiteral("jobs")).toArray().at(0).toObject()
                                 .value(QStringLiteral("items")).toArray().at(0).toObject();
    QCOMPARE(item.value(QStringLiteral("state")).toString(), QStringLiteral("completed"));
    QCOMPARE(item.value(QStringLiteral("format")).toString(), QStringLiteral("mp3"));
    QVERIFY(!item.contains(QStringLiteral("streamUrl")));
}

void DownloadManagerTest::twoTransfersAtOnce()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {},
                                    {fixture.song(QStringLiteral("A")), fixture.song(QStringLiteral("B")),
                                     fixture.song(QStringLiteral("C"))}));
    QCOMPARE(fixture.fetcher.pending.size(), size_t(2));
    QCOMPARE(fixture.manager.running(), 2);
    fixture.fetcher.finish("a");
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(2));
    QCOMPARE(fixture.fetcher.pending.back().url, QUrl(QStringLiteral("https://stream.example.test/C")));
}

void DownloadManagerTest::partialFilesResume()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("One"))}));
    const QString part = fixture.fetcher.pending.front().path;
    // Half the file arrives, then the listener pauses.
    QFile half(part);
    QVERIFY(half.open(QIODevice::WriteOnly));
    half.write("first-");
    half.close();
    const QString jobId = fixture.manager.snapshot().jobs.first().job.id;
    fixture.manager.pauseJob(jobId);
    QVERIFY(fixture.fetcher.pending.front().stop());
    fixture.fetcher.stopOldest();
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Paused);
    fixture.manager.resumeJob(jobId);
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(1));
    QCOMPARE(fixture.fetcher.pending.front().offset, std::uint64_t(6));
    fixture.fetcher.finish("second", true);
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Completed);
    QFile done(fixture.manager.readyPath(fixture.song(QStringLiteral("One")).stableKey));
    QVERIFY(done.open(QIODevice::ReadOnly));
    QCOMPARE(done.readAll(), QByteArray("first-second"));
}

void DownloadManagerTest::rulesHoldTheQueue()
{
    Fixture fixture;
    fixture.metered = true;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("One"))}));
    QVERIFY(fixture.fetcher.pending.empty());
    // Allowed on a metered connection once the rule is off.
    fixture.manager.setAvoidMeteredNetworks(false);
    QCOMPARE(fixture.fetcher.pending.size(), size_t(1));
    fixture.fetcher.finish("x");
    QTRY_COMPARE(fixture.manager.running(), 0);

    fixture.battery = true;
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("Two"))}));
    QVERIFY(fixture.fetcher.pending.empty());
    fixture.manager.setDownloadOnBattery(true);
    QCOMPARE(fixture.fetcher.pending.size(), size_t(1));

    // Pause all holds what is queued; the rule is kept in the state file.
    fixture.manager.pauseAll();
    QVERIFY(fixture.fetcher.pending.front().stop());
    QVERIFY(fixture.manager.snapshot().allPaused);
}

void DownloadManagerTest::itemsOfAnotherScopeFail()
{
    Fixture fixture;
    fixture.metered = true;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("One"))}));
    fixture.scope = QStringLiteral("download|Account:1:2\n");
    fixture.metered = false;
    fixture.manager.refreshScheduling();
    QCOMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Failed);
    QCOMPARE(fixture.manager.snapshot().jobs.first().error, QStringLiteral("Queued while a different account was signed in"));
    QVERIFY(fixture.fetcher.pending.empty());
}

void DownloadManagerTest::earlierStateFileIsRead()
{
    QTemporaryDir folder;
    QDir().mkpath(folder.filePath(QStringLiteral("Offline")));
    const QString kept = folder.filePath(QStringLiteral("Offline/Kept-ABCDEF12.mp3"));
    QFile keptFile(kept);
    QVERIFY(keptFile.open(QIODevice::WriteOnly));
    keptFile.write("kept");
    keptFile.close();
    const QJsonObject item1{{QStringLiteral("id"), QStringLiteral("I1")}, {QStringLiteral("stableKey"), QStringLiteral("k1")},
                            {QStringLiteral("sourceUrl"), QStringLiteral("https://m.example.test/1")},
                            {QStringLiteral("title"), QStringLiteral("Kept")}, {QStringLiteral("filePath"), kept},
                            {QStringLiteral("format"), QStringLiteral("mp3")}, {QStringLiteral("state"), QStringLiteral("completed")},
                            {QStringLiteral("bytesDownloaded"), 4}, {QStringLiteral("bytesTotal"), 4}, {QStringLiteral("finishedAt"), 1700000000}};
    const QJsonObject item2{{QStringLiteral("id"), QStringLiteral("I2")}, {QStringLiteral("stableKey"), QStringLiteral("k2")},
                            {QStringLiteral("sourceUrl"), QStringLiteral("https://m.example.test/2")},
                            {QStringLiteral("title"), QStringLiteral("Gone")},
                            {QStringLiteral("filePath"), folder.filePath(QStringLiteral("Offline/Gone.mp3"))},
                            {QStringLiteral("state"), QStringLiteral("completed")}};
    const QJsonObject item3{{QStringLiteral("id"), QStringLiteral("I3")}, {QStringLiteral("stableKey"), QStringLiteral("k3")},
                            {QStringLiteral("sourceUrl"), QStringLiteral("https://m.example.test/3")},
                            {QStringLiteral("title"), QStringLiteral("Mid")}, {QStringLiteral("state"), QStringLiteral("downloading")}};
    const QJsonObject root{{QStringLiteral("schemaVersion"), 1},
                           {QStringLiteral("allPaused"), true},
                           {QStringLiteral("jobs"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("J1")},
                                                                           {QStringLiteral("title"), QStringLiteral("Old")},
                                                                           {QStringLiteral("hidden"), false},
                                                                           {QStringLiteral("items"), QJsonArray{item1, item2, item3}}}}}};
    QFile state(folder.filePath(QStringLiteral("Downloads.json")));
    QVERIFY(state.open(QIODevice::WriteOnly));
    state.write(QJsonDocument(root).toJson());
    state.close();

    lmp::core::SettingsStore settings(folder.filePath(QStringLiteral("Settings.json")));
    lmp::core::UserDataOperationGate gate;
    FakeFetcher fetcher;
    DownloadManager manager({.settings = settings,
                             .gate = gate,
                             .fetcher = fetcher,
                             .stateFile = folder.filePath(QStringLiteral("Downloads.json")),
                             .defaultFolder = folder.filePath(QStringLiteral("Offline")),
                             .resolve = {},
                             .scopePrefix = {},
                             .metered = {},
                             .onBattery = {}});
    manager.initialize();
    QCOMPARE(manager.readyPath(QStringLiteral("k1")), kept);
    QVERIFY(manager.readyPath(QStringLiteral("k2")).isEmpty());
    const auto snapshot = manager.snapshot();
    QVERIFY(snapshot.allPaused);
    QCOMPARE(snapshot.jobs.size(), 1);
    const QList<lmp::core::DownloadItem> items = snapshot.jobs.first().job.items;
    QCOMPARE(items.at(0).state, DownloadItemState::Completed);
    // A finished item whose file is gone, and an interrupted one, wait again.
    QCOMPARE(items.at(1).state, DownloadItemState::Queued);
    QCOMPARE(items.at(2).state, DownloadItemState::Queued);
    QCOMPARE(snapshot.offlineTracks, 1);
    // All paused: nothing starts.
    QVERIFY(fetcher.pending.empty());
}

QTEST_GUILESS_MAIN(DownloadManagerTest)
#include "tst_download_manager.moc"
