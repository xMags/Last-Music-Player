// The offline download manager over a fake fetcher and fake rules: the
// earlier Downloads.json, resuming a partial file, file names, the two-transfer
// cap, the network and battery rules, pausing, and items of another scope.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QPromise>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

#include <deque>

#include <windows.h>
#include <memory>
#include <vector>

#include "TempLibrary.h"
#include "FakeTagReader.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/downloads/DownloadExtrasService.h"
#include "core/downloads/DownloadLibraryIndex.h"
#include "core/downloads/DownloadManager.h"
#include "core/downloads/PendingExtrasStore.h"
#include "core/downloads/SongFileExtras.h"
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
        QString ifRange;
        Stop stop;
        std::shared_ptr<QPromise<Result<Outcome>>> promise;
    };

    QFuture<Result<Outcome>> fetch(const QUrl& url, const QString& filePath, std::uint64_t offset,
                                   const QString& ifRange, Stop stop, Progress, Validated validated) override
    {
        auto promise = std::make_shared<QPromise<Result<Outcome>>>();
        promise->start();
        // The server names its file at once, as a real answer's headers do.
        if (validated && !validator.isEmpty()) {
            validated(validator);
        }
        pending.push_back({url, filePath, offset, ifRange, std::move(stop), promise});
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

    // Fails the oldest transfer with an HTTP status.
    void failOldest(int status)
    {
        QVERIFY(!pending.empty());
        Pending next = pending.front();
        pending.pop_front();
        next.promise->addResult(Result<Outcome>(
            lmp::core::Failure::of(lmp::core::FailureKind::Server, QStringLiteral("refused"), status)));
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

    // What the fake server calls its file (an ETag); empty: it names none.
    QString validator = QStringLiteral("\"v1\"");
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
    QString scope = QStringLiteral("download|Account:owner-a\n");
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
    void removeAllDeletesFilesAndDropsTransfers();
    void oneSongOfAJobCanBeCancelled();
    void oneSongIsRemovedEverywhere();
    void readingTheListIsAnnounced();
    void anUnreadableListIsSetAside();
    void aListThatCannotBeOpenedIsNeverOverwritten();
    void anUnreadableListThatCannotBeMovedIsNeverOverwritten();
    void aResumeAsksForTheSameFile();
    void aPartWithoutAValidatorStartsOver();
    void aRangePastTheEndStartsOverOnce();
    void earlierKeysAreStillFound();
    void extrasAreWrittenOnceForEverySong();
    void extrasOfAPlayingSongAreKeptAndWrittenOffline();
    void aFailedFetchKeepsNothingAndWaits();
    void downloadsBecomeSongsOfThisPc();
};

void DownloadManagerTest::removeAllDeletesFilesAndDropsTransfers()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {},
                                    {fixture.song(QStringLiteral("A")), fixture.song(QStringLiteral("B")),
                                     fixture.song(QStringLiteral("C"))}));
    fixture.fetcher.finish("a");
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("A")), DownloadItemState::Completed);
    const QString finished = fixture.manager.readyPath(fixture.song(QStringLiteral("A")).stableKey);
    QVERIFY(QFileInfo::exists(finished));
    // B is still transferring when everything goes.
    QVERIFY(!fixture.fetcher.pending.empty());
    const QString runningPart = fixture.fetcher.pending.front().path;

    // Not while "Delete all data" holds the gate: nothing changes.
    QVERIFY(fixture.gate.closeAdmissions());
    QVERIFY(!fixture.manager.removeAllDownloads());
    QCOMPARE(fixture.manager.snapshot().offlineTracks, 1);
    fixture.gate.reopen();

    QVERIFY(fixture.manager.removeAllDownloads());
    QVERIFY(!QFileInfo::exists(finished));
    QVERIFY(fixture.manager.snapshot().jobs.isEmpty());
    QCOMPARE(fixture.manager.snapshot().offlineTracks, 0);
    QCOMPARE(fixture.manager.snapshot().offlineBytes, std::uint64_t{0});

    // The dropped transfer returns later; its partial file goes and nothing
    // comes back.
    fixture.fetcher.finish("b");
    QTRY_VERIFY(!QFileInfo::exists(runningPart));
    QVERIFY(fixture.manager.snapshot().jobs.isEmpty());
}

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
    // Another account signed in.
    fixture.scope = QStringLiteral("download|Account:owner-b\n");
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

void DownloadManagerTest::oneSongOfAJobCanBeCancelled()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {},
                                    {fixture.song(QStringLiteral("One")), fixture.song(QStringLiteral("Two")),
                                     fixture.song(QStringLiteral("Three"))}));
    const auto job = fixture.manager.snapshot().jobs.first().job;
    QCOMPARE(job.items.size(), 3);
    const QString third = job.items.at(2).id;
    fixture.manager.cancelItem(job.id, third);
    QCOMPARE(fixture.manager.snapshot().jobs.first().job.items.size(), 2);
    // A finished song stays.
    fixture.fetcher.finish("x");
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Completed);
    fixture.manager.cancelItem(job.id, job.items.at(0).id);
    QCOMPARE(fixture.manager.snapshot().jobs.first().job.items.size(), 2);
    // The last unfinished song gone, a job of finished songs remains;
    // one with no songs left goes.
    fixture.manager.cancelItem(job.id, job.items.at(1).id);
    QCOMPARE(fixture.manager.snapshot().jobs.first().job.items.size(), 1);
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Solo"), {}, {}, {fixture.song(QStringLiteral("Four"))}));
    const auto solo = fixture.manager.snapshot().jobs.last().job;
    fixture.manager.cancelItem(solo.id, solo.items.first().id);
    QCOMPARE(fixture.manager.snapshot().jobs.size(), 1);
}

void DownloadManagerTest::oneSongIsRemovedEverywhere()
{
    Fixture fixture;
    fixture.manager.initialize();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {},
                                    {fixture.song(QStringLiteral("One")), fixture.song(QStringLiteral("Two"))}));
    fixture.fetcher.finish("one");
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Completed);
    const QString file = fixture.manager.readyPath(fixture.scope + QStringLiteral("One"));
    QVERIFY(QFileInfo::exists(file));
    // Its saved lyrics go with it.
    QFile lrc(lmp::core::downloads::lrcPathFor(file));
    QVERIFY(lrc.open(QIODevice::WriteOnly));
    lrc.close();

    QVERIFY(fixture.manager.removeSong(fixture.scope + QStringLiteral("One")));
    QVERIFY(!QFileInfo::exists(file));
    QVERIFY(!QFileInfo::exists(lmp::core::downloads::lrcPathFor(file)));
    QVERIFY(fixture.manager.readyPath(fixture.scope + QStringLiteral("One")).isEmpty());
    QCOMPARE(fixture.manager.snapshot().jobs.first().job.items.size(), 1);
    // The last song gone, its job goes too; an unknown song changes nothing.
    QVERIFY(fixture.manager.removeSong(fixture.scope + QStringLiteral("Two")));
    QVERIFY(fixture.manager.snapshot().jobs.isEmpty());
    QVERIFY(fixture.manager.removeSong(fixture.scope + QStringLiteral("Nobody")));
}

void DownloadManagerTest::readingTheListIsAnnounced()
{
    // Views built before the list was read (the Offline page) learn of it.
    Fixture fixture;
    const std::uint64_t before = fixture.manager.snapshot().revision;
    QSignalSpy changed(&fixture.manager, &lmp::core::DownloadManager::changed);
    fixture.manager.initialize();
    QVERIFY(changed.count() >= 1);
    QVERIFY(fixture.manager.snapshot().revision != before);
}

namespace {

// A one-job list, paused so nothing starts.
QByteArray oneJobList()
{
    const QJsonObject item{{QStringLiteral("id"), QStringLiteral("I1")},
                           {QStringLiteral("title"), QStringLiteral("Old")},
                           {QStringLiteral("sourceUrl"), QStringLiteral("https://music.example.test/Old")},
                           {QStringLiteral("state"), QStringLiteral("queued")}};
    const QJsonObject job{{QStringLiteral("id"), QStringLiteral("J1")},
                          {QStringLiteral("title"), QStringLiteral("Old")},
                          {QStringLiteral("items"), QJsonArray{item}}};
    return QJsonDocument(QJsonObject{{QStringLiteral("schemaVersion"), 1},
                                     {QStringLiteral("allPaused"), true},
                                     {QStringLiteral("jobs"), QJsonArray{job}}})
        .toJson(QJsonDocument::Compact);
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

void DownloadManagerTest::anUnreadableListIsSetAside()
{
    Fixture fixture;
    const QString list = fixture.folder.filePath(QStringLiteral("Downloads.json"));
    {
        QFile file(list);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not the list");
    }
    fixture.manager.initialize();
    QVERIFY(fixture.manager.snapshot().jobs.isEmpty());
    // The unreadable bytes are kept aside, never overwritten.
    const QStringList aside = QDir(fixture.folder.path()).entryList({QStringLiteral("Downloads.json.corrupt-*")});
    QCOMPARE(aside.size(), 1);
    QCOMPARE(readAll(fixture.folder.filePath(aside.first())), QByteArray("{ not the list"));
    fixture.manager.pauseAll();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("New"), {}, {}, {fixture.song(QStringLiteral("New"))}));
    QVERIFY(readAll(list).contains("New"));
}

void DownloadManagerTest::aListThatCannotBeOpenedIsNeverOverwritten()
{
    Fixture fixture;
    const QString list = fixture.folder.filePath(QStringLiteral("Downloads.json"));
    {
        QFile file(list);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(oneJobList());
    }
    // Held by another program with no sharing, as a scanner might.
    HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(list).utf16()), GENERIC_READ,
                              0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(held != INVALID_HANDLE_VALUE);
    fixture.manager.initialize();
    QVERIFY(fixture.manager.snapshot().jobs.isEmpty());
    fixture.manager.pauseAll();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("New"), {}, {}, {fixture.song(QStringLiteral("New"))}));
    CloseHandle(held);
    // Nothing was written over it while it could not be read...
    QCOMPARE(readAll(list), oneJobList());
    // ...and once it reads, both the old list and the new song are kept.
    QTRY_COMPARE_WITH_TIMEOUT(fixture.manager.snapshot().jobs.size(), 2, DownloadManager::LoadRetryMs * 3);
    QVERIFY(readAll(list).contains("Old"));
    QVERIFY(readAll(list).contains("New"));
}

void DownloadManagerTest::anUnreadableListThatCannotBeMovedIsNeverOverwritten()
{
    Fixture fixture;
    const QString list = fixture.folder.filePath(QStringLiteral("Downloads.json"));
    {
        QFile file(list);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not the list");
    }
    // Readable, but held without delete sharing, so it cannot be moved aside.
    HANDLE held = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(list).utf16()), GENERIC_READ,
                              FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(held != INVALID_HANDLE_VALUE);
    fixture.manager.initialize();
    CloseHandle(held);
    fixture.manager.pauseAll();
    QVERIFY(fixture.manager.enqueue(QStringLiteral("New"), {}, {}, {fixture.song(QStringLiteral("New"))}));
    QCOMPARE(readAll(list), QByteArray("{ not the list"));
}

void DownloadManagerTest::earlierKeysAreStillFound()
{
    // Downloaded by an earlier version, whose key carried run counters.
    Fixture fixture;
    fixture.manager.initialize();
    const QString song = QStringLiteral("remote|tag-b|https://music.example.test/One");
    DownloadTrackRequest earlier = fixture.song(QStringLiteral("One"));
    earlier.stableKey = QStringLiteral("download|Account:3:7\n") + song;
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {earlier}));
    // It still runs in the same mode (an earlier key names no account).
    QTRY_VERIFY(!fixture.fetcher.pending.empty());
    fixture.fetcher.finish("one");
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Completed);

    // Today's key of the same song finds it, and does not queue it again.
    const QString today = QStringLiteral("download|Account:owner-a\n") + song;
    QVERIFY(!fixture.manager.readyPath(today).isEmpty());
    DownloadTrackRequest again = fixture.song(QStringLiteral("One"));
    again.stableKey = today;
    QVERIFY(!fixture.manager.enqueue(QStringLiteral("Again"), {}, {}, {again}));
}

namespace {

// Starts one song, writes half of it, and pauses it.
QString pauseHalfway(Fixture& fixture)
{
    fixture.manager.initialize();
    if (!fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, {fixture.song(QStringLiteral("One"))})) {
        return {};
    }
    const QString part = fixture.fetcher.pending.front().path;
    QFile half(part);
    if (!half.open(QIODevice::WriteOnly)) {
        return {};
    }
    half.write("first-");
    half.close();
    const QString jobId = fixture.manager.snapshot().jobs.first().job.id;
    fixture.manager.pauseJob(jobId);
    fixture.fetcher.stopOldest();
    QCoreApplication::processEvents();
    return jobId;
}

} // namespace

void DownloadManagerTest::aResumeAsksForTheSameFile()
{
    Fixture fixture;
    const QString jobId = pauseHalfway(fixture);
    QVERIFY(!jobId.isEmpty());
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Paused);
    fixture.manager.resumeJob(jobId);
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(1));
    // The range is asked for with the file's validator.
    QCOMPARE(fixture.fetcher.pending.front().offset, std::uint64_t(6));
    QCOMPARE(fixture.fetcher.pending.front().ifRange, QStringLiteral("\"v1\""));
}

void DownloadManagerTest::aPartWithoutAValidatorStartsOver()
{
    Fixture fixture;
    fixture.fetcher.validator.clear(); // the server names no validator
    const QString jobId = pauseHalfway(fixture);
    QVERIFY(!jobId.isEmpty());
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Paused);
    fixture.manager.resumeJob(jobId);
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(1));
    // Its bytes may be another file's: they go, and the song starts over.
    QCOMPARE(fixture.fetcher.pending.front().offset, std::uint64_t(0));
    QVERIFY(!QFileInfo::exists(fixture.fetcher.pending.front().path));
}

void DownloadManagerTest::aRangePastTheEndStartsOverOnce()
{
    Fixture fixture;
    const QString jobId = pauseHalfway(fixture);
    QVERIFY(!jobId.isEmpty());
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Paused);
    fixture.manager.resumeJob(jobId);
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(1));
    const QString part = fixture.fetcher.pending.front().path;
    fixture.fetcher.failOldest(416);
    // The part goes and the song is fetched again from the start.
    QTRY_COMPARE(fixture.fetcher.pending.size(), size_t(1));
    QCOMPARE(fixture.fetcher.pending.front().offset, std::uint64_t(0));
    QVERIFY(!QFileInfo::exists(part));
    // A second 416 is a failure like any other.
    fixture.fetcher.failOldest(416);
    QTRY_COMPARE(fixture.stateOf(QStringLiteral("One")), DownloadItemState::Failed);
}

namespace {

// The extras service over the fixture's list, with fakes for the network
// and the file work.
struct ExtrasFixture {
    explicit ExtrasFixture(Fixture& downloads)
        : pending(keptFolder.path())
        , service({.downloads = downloads.manager,
                   .pending = pending,
                   .canFetch = [this](const lmp::core::DownloadItem&) { return online; },
                   .cover =
                       [this](const lmp::core::DownloadItem& item) {
                           ++coverAsks;
                           return QtFuture::makeReadyValueFuture(
                               coverFails ? Result<QByteArray>(lmp::core::Failure::of(lmp::core::FailureKind::Network))
                                          : Result<QByteArray>(QByteArray("cover of ") + item.title.toUtf8()));
                       },
                   .lyrics =
                       [](const lmp::core::DownloadItem&) {
                           lmp::core::lyrics::LyricsResult words;
                           words.found = true;
                           words.plain = QStringLiteral("la la");
                           return QtFuture::makeReadyValueFuture(Result<lmp::core::lyrics::LyricsResult>(words));
                       },
                   .inUse = [this](const QString& path) { return path == playing; },
                   .write =
                       [this](const QString& path, const lmp::core::downloads::SongFileExtras& extras) {
                           written.append(QFileInfo(path).fileName().section(QLatin1Char('-'), 0, 0));
                           lastExtras = extras;
                           return QtFuture::makeReadyValueFuture(Result<void>::success());
                       },
                   .stepGapMs = 1})
    {
    }

    bool online = true;
    bool coverFails = false;
    int coverAsks = 0;
    QString playing;
    QStringList written;
    lmp::core::downloads::SongFileExtras lastExtras;
    QTemporaryDir keptFolder;
    lmp::core::downloads::PendingExtrasStore pending;
    lmp::core::downloads::DownloadExtrasService service;
};

void finishSongs(Fixture& fixture, const QStringList& titles)
{
    QList<DownloadTrackRequest> songs;
    for (const QString& title : titles) {
        DownloadTrackRequest song = fixture.song(title);
        song.artworkUrl = QStringLiteral("https://art.example.test/") + title;
        songs.append(song);
    }
    QVERIFY(fixture.manager.enqueue(QStringLiteral("Job"), {}, {}, songs));
    for (const QString& title : titles) {
        QTRY_VERIFY(!fixture.fetcher.pending.empty());
        fixture.fetcher.finish(title.toUtf8());
        QTRY_COMPARE(fixture.stateOf(title), DownloadItemState::Completed);
    }
}

} // namespace

void DownloadManagerTest::extrasAreWrittenOnceForEverySong()
{
    Fixture fixture;
    fixture.manager.initialize();
    finishSongs(fixture, {QStringLiteral("One"), QStringLiteral("Two")});
    ExtrasFixture extras(fixture);
    extras.service.start();
    QTRY_COMPARE(extras.written.size(), 2);
    QCOMPARE(extras.lastExtras.plainLyrics, QStringLiteral("la la"));
    QVERIFY(extras.lastExtras.cover.startsWith("cover of "));
    for (const lmp::core::DownloadItem& item : fixture.manager.finishedSongs()) {
        QCOMPARE(item.extrasVersion, lmp::core::downloads::DownloadExtrasService::CurrentVersion);
    }
    // Remembered: nothing is written again, in this run or the next.
    QTest::qWait(50);
    QCOMPARE(extras.written.size(), 2);
    DownloadManager reread({.settings = fixture.settings,
                            .gate = fixture.gate,
                            .fetcher = fixture.fetcher,
                            .stateFile = fixture.folder.filePath(QStringLiteral("Downloads.json")),
                            .defaultFolder = fixture.folder.filePath(QStringLiteral("Offline")),
                            .resolve = {},
                            .scopePrefix = [&fixture]() { return fixture.scope; },
                            .metered = []() { return false; },
                            .onBattery = []() { return false; }});
    reread.initialize();
    QCOMPARE(reread.finishedSongs().size(), 2);
    for (const lmp::core::DownloadItem& item : reread.finishedSongs()) {
        QCOMPARE(item.extrasVersion, lmp::core::downloads::DownloadExtrasService::CurrentVersion);
    }
}

void DownloadManagerTest::extrasOfAPlayingSongAreKeptAndWrittenOffline()
{
    Fixture fixture;
    fixture.manager.initialize();
    finishSongs(fixture, {QStringLiteral("One"), QStringLiteral("Two")});
    const QString two = fixture.manager.readyPath(fixture.scope + QStringLiteral("Two"));
    ExtrasFixture extras(fixture);
    extras.online = false;
    extras.playing = two;
    extras.service.start();
    QTest::qWait(50);
    QVERIFY(extras.written.isEmpty());
    QCOMPARE(extras.coverAsks, 0);

    // Online: both are fetched; the playing one is only kept.
    extras.online = true;
    extras.service.refresh();
    QTRY_COMPARE(extras.coverAsks, 2);
    QTRY_COMPARE(extras.written, QStringList({QStringLiteral("One")}));
    QVERIFY(extras.pending.contains(two));
    QCOMPARE(extras.pending.load(two)->plainLyrics, QStringLiteral("la la"));

    // Offline again, and the song stops: its file takes what was kept,
    // without asking the network.
    extras.online = false;
    extras.playing.clear();
    extras.service.refresh();
    QTRY_COMPARE(extras.written.size(), 2);
    QCOMPARE(extras.coverAsks, 2);
    QCOMPARE(extras.lastExtras.plainLyrics, QStringLiteral("la la"));
    QVERIFY(!extras.pending.contains(two));
    for (const lmp::core::DownloadItem& item : fixture.manager.finishedSongs()) {
        QCOMPARE(item.extrasVersion, lmp::core::downloads::DownloadExtrasService::CurrentVersion);
    }
}

void DownloadManagerTest::aFailedFetchKeepsNothingAndWaits()
{
    Fixture fixture;
    fixture.manager.initialize();
    finishSongs(fixture, {QStringLiteral("One")});
    const QString one = fixture.manager.readyPath(fixture.scope + QStringLiteral("One"));
    ExtrasFixture extras(fixture);
    extras.coverFails = true;
    extras.service.start();
    QTRY_COMPARE(extras.coverAsks, 1);
    QTest::qWait(50);
    // Not taken for "no cover": nothing kept or written, and not asked again
    // before the retry time.
    QCOMPARE(extras.coverAsks, 1);
    QVERIFY(extras.written.isEmpty());
    QVERIFY(!extras.pending.contains(one));
}

void DownloadManagerTest::downloadsBecomeSongsOfThisPc()
{
    Fixture fixture;
    fixture.manager.initialize();
    finishSongs(fixture, {QStringLiteral("One"), QStringLiteral("Two")});
    TempLibrary library;
    QVERIFY(library.ok());
    lmp::core::db::TrackRepository tracks(library.database());
    lmp::test::FakeTagReader tags;
    lmp::core::downloads::DownloadLibraryIndex index(
        {.downloads = fixture.manager, .tags = tags, .tracks = tracks, .gate = fixture.gate, .settleMs = 10});
    QSignalSpy indexed(&index, &lmp::core::downloads::DownloadLibraryIndex::indexed);
    index.start();
    QTRY_COMPARE(indexed.size(), 1);

    lmp::core::db::LibraryQueries queries(library.database());
    const QList<lmp::core::Track> songs = queries.loadTracks(false, true);
    QCOMPARE(songs.size(), 2);
    for (const lmp::core::Track& song : songs) {
        QVERIFY(!song.isRemote());
        QVERIFY(QFileInfo::exists(song.filePath));
        QCOMPARE(song.artist, QStringLiteral("Artist"));
        QCOMPARE(song.durationSeconds, 125.4);
        QCOMPARE(song.sourceUrl, QStringLiteral("https://music.example.test/") + song.title);
        QCOMPARE(song.artworkUrl, QStringLiteral("https://art.example.test/") + song.title);
        QVERIFY(song.dateAddedSortKey > 0);
    }
    QCOMPARE(tags.reads.load(), 2);

    // A change that leaves the songs as they are writes nothing.
    fixture.manager.setExtrasWritten(songs.front().filePath, 1);
    QTest::qWait(60);
    QCOMPARE(indexed.size(), 1);

    // After the library is emptied they come back, lengths read again.
    QVERIFY(tracks.clearAllUserData().isOk());
    index.reindex();
    QTRY_COMPARE(indexed.size(), 2);
    QCOMPARE(queries.loadTracks(false, true).size(), 2);
    QCOMPARE(tags.reads.load(), 4);

    // A removed download leaves Library; the other's length is not read again.
    QVERIFY(fixture.manager.removeSong(fixture.scope + QStringLiteral("One")));
    QTRY_COMPARE(indexed.size(), 3);
    const QList<lmp::core::Track> left = queries.loadTracks(false, true);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left.front().title, QStringLiteral("Two"));
    QCOMPARE(tags.reads.load(), 4);
}

QTEST_GUILESS_MAIN(DownloadManagerTest)
#include "tst_download_manager.moc"
