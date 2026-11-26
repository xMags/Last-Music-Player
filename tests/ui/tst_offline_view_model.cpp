#include <QFile>
#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "../core/InlineDatabaseReadRunner.h"
#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/DatabaseReader.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/downloads/DownloadManager.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "platform/IFolderPicker.h"
#include "ui/downloads/OfflineViewModel.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::DownloadManager;
using lmp::core::DownloadTrackRequest;
using lmp::core::IDownloadFetcher;
using lmp::core::Result;
using lmp::ui::OfflineViewModel;

namespace {

// Holds every transfer open and keeps its progress callback, so the test
// reports progress by hand.
class HeldFetcher final : public IDownloadFetcher {
public:
    QFuture<Result<Outcome>> fetch(const QUrl&, const QString& filePath, std::uint64_t, const QString&, Stop,
                                   Progress progress, Validated) override
    {
        auto promise = std::make_shared<QPromise<Result<Outcome>>>();
        promise->start();
        paths.push_back(filePath);
        progressOf.push_back(std::move(progress));
        kept.push_back(promise);
        return promise->future();
    }

    // Finishes transfer `index`: writes its file and answers.
    void complete(std::size_t index)
    {
        QFile file(paths.at(index));
        if (file.open(QIODevice::WriteOnly)) {
            file.write("audio");
        }
        file.close();
        kept.at(index)->addResult(Result<Outcome>(Outcome{.stopped = false, .appended = false,
                                                          .mediaType = QStringLiteral("audio/mpeg")}));
        kept.at(index)->finish();
    }

    std::deque<Progress> progressOf;
    std::vector<QString> paths;
    std::vector<std::shared_ptr<QPromise<Result<Outcome>>>> kept;
};

class NoFolderPicker final : public lmp::platform::IFolderPicker {
public:
    std::optional<QString> pickFolder(quintptr, const QString&) override { return std::nullopt; }
};

const QString Scope = QStringLiteral("download|Account:owner-a\n");

[[nodiscard]] DownloadTrackRequest song(const QString& title)
{
    return {Scope + title, QStringLiteral("https://music.example.test/") + title, QStringLiteral("tag-b"), title,
            QStringLiteral("Artist"), QStringLiteral("Album"), {}};
}

} // namespace

// The download marks of song rows: read again only when a mark changes.
class OfflineViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void aProgressTickThatChangesAPercentNotifiesTheMarks();
    void aReloadWithTheSameMarksDoesNotNotifyThem();
    void anOutOfDateLibraryReadIsDropped();

private:
    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    lmp::test::InlineDatabaseReadRunner* m_runner = nullptr;
    std::unique_ptr<lmp::core::db::DatabaseReader> m_reader;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::TrackActions> m_actions;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<lmp::ui::ToastQueue> m_toasts;
    std::unique_ptr<QTemporaryDir> m_folder;
    std::unique_ptr<HeldFetcher> m_fetcher;
    std::unique_ptr<NoFolderPicker> m_picker;
    std::unique_ptr<DownloadManager> m_downloads;
    std::unique_ptr<OfflineViewModel> m_model;
};

void OfflineViewModelTest::init()
{
    m_player = std::make_unique<PlayerFixture>();
    QVERIFY(m_player->ok());
    m_modes = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_modes->modes.start();
    m_controller = m_player->makeController();
    lmp::core::db::Database& database = m_player->library.database();
    auto runner = std::make_unique<lmp::test::InlineDatabaseReadRunner>(database);
    m_runner = runner.get();
    m_reader = std::make_unique<lmp::core::db::DatabaseReader>(std::move(runner));
    m_collections = std::make_unique<lmp::core::db::CollectionRepository>(database);
    m_groups = std::make_unique<lmp::core::db::GroupQueries>(database);
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
    m_actions = std::make_unique<lmp::core::TrackActions>(lmp::core::TrackActions::Dependencies{
        *m_controller, m_player->tracks, *m_collections, *m_groups, m_modes->modes, *m_gate});
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_toasts = std::make_unique<lmp::ui::ToastQueue>();
    m_folder = std::make_unique<QTemporaryDir>();
    QVERIFY(m_folder->isValid());
    m_fetcher = std::make_unique<HeldFetcher>();
    m_picker = std::make_unique<NoFolderPicker>();
    m_downloads = std::make_unique<DownloadManager>(DownloadManager::Dependencies{
        .settings = m_player->settings,
        .gate = *m_gate,
        .fetcher = *m_fetcher,
        .stateFile = m_folder->filePath(QStringLiteral("Downloads.json")),
        .defaultFolder = m_folder->filePath(QStringLiteral("Offline")),
        .resolve =
            [](const lmp::core::DownloadItem& item) {
                return QtFuture::makeReadyValueFuture(
                    Result<QUrl>(QUrl(QStringLiteral("https://stream.example.test/") + item.title)));
            },
        .scopePrefix = []() { return Scope; },
        .metered = []() { return false; },
        .onBattery = []() { return false; }});
    m_downloads->initialize();
    m_model = std::make_unique<OfflineViewModel>(OfflineViewModel::Parts{
        .downloads = *m_downloads,
        .actions = *m_actions,
        .reader = *m_reader,
        .trackMenu = *m_menu,
        .toasts = *m_toasts,
        .picker = *m_picker,
        .currentTrack = []() { return std::optional<lmp::core::Track>(); },
        .canDownload = []() { return true; },
        .stableKey = [](const lmp::core::Track&) { return QString(); }});
}

void OfflineViewModelTest::cleanup()
{
    m_model.reset();
    m_downloads.reset();
    m_picker.reset();
    m_fetcher.reset();
    m_folder.reset();
    m_toasts.reset();
    m_menu.reset();
    m_actions.reset();
    m_gate.reset();
    m_groups.reset();
    m_collections.reset();
    m_reader.reset();
    m_runner = nullptr;
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

void OfflineViewModelTest::aProgressTickThatChangesAPercentNotifiesTheMarks()
{
    const QString address = song(QStringLiteral("A")).sourceUrl;
    QSignalSpy marks(m_model.get(), &OfflineViewModel::marksChanged);
    QVERIFY(m_downloads->enqueue(QStringLiteral("Job"), {}, {}, {song(QStringLiteral("A"))}));
    // Queued is a new mark.
    QVERIFY(marks.count() >= 1);
    QTRY_COMPARE(m_fetcher->progressOf.size(), std::size_t{1});
    QTRY_COMPARE(m_model->markOf(address).value(QStringLiteral("state")).toString(), QStringLiteral("downloading"));

    const int revision = m_model->markRevision();
    marks.clear();
    QSignalSpy changes(m_model.get(), &OfflineViewModel::changed);
    // Progress is passed on at most every 400 ms.
    QTest::qWait(450);
    m_fetcher->progressOf.front()(50, 100);
    QCOMPARE(m_model->markOf(address).value(QStringLiteral("percent")).toInt(), 50);
    QCOMPARE(marks.count(), 1);
    QCOMPARE(m_model->markRevision(), revision + 1);
    QVERIFY(changes.count() >= 1);
}

void OfflineViewModelTest::aReloadWithTheSameMarksDoesNotNotifyThem()
{
    const QString address = song(QStringLiteral("A")).sourceUrl;
    QVERIFY(m_downloads->enqueue(QStringLiteral("Job"), {}, {}, {song(QStringLiteral("A"))}));
    QTRY_COMPARE(m_fetcher->progressOf.size(), std::size_t{1});
    QTest::qWait(450);
    m_fetcher->progressOf.front()(50, 100);
    QCOMPARE(m_model->markOf(address).value(QStringLiteral("percent")).toInt(), 50);

    const int revision = m_model->markRevision();
    QSignalSpy marks(m_model.get(), &OfflineViewModel::marksChanged);
    QSignalSpy changes(m_model.get(), &OfflineViewModel::changed);
    // More bytes, the same percent: the strip's figures change, no mark does.
    QTest::qWait(450);
    m_fetcher->progressOf.front()(51, 101);
    QCOMPARE(m_model->markOf(address).value(QStringLiteral("percent")).toInt(), 50);
    QCOMPARE(changes.count(), 1);
    QCOMPARE(marks.count(), 0);
    QCOMPARE(m_model->markRevision(), revision);
}

// The collection tabs read the library on the reader and keep what they show
// until its answer comes. A reload while a read is under way drops that
// read's answer and reads once more, however many reloads came meanwhile.
void OfflineViewModelTest::anOutOfDateLibraryReadIsDropped()
{
    QVERIFY(m_downloads->enqueue(QStringLiteral("Job"), {}, {}, {song(QStringLiteral("A"))}));
    QTRY_COMPARE(m_fetcher->progressOf.size(), std::size_t{1});
    m_fetcher->complete(0);
    // The finished song changes the set of downloads: one read.
    QTRY_VERIFY(m_runner->posted >= 1);
    settle();
    const int readsBefore = m_runner->posted;

    m_runner->holding = true;
    m_model->refresh();
    QCOMPARE(m_runner->posted, readsBefore + 1);
    // Two more reloads while it is held: no read yet, that one is out of date.
    m_model->refresh();
    m_model->refresh();
    QCOMPARE(m_runner->posted, readsBefore + 1);

    QSignalSpy changes(m_model.get(), &OfflineViewModel::changed);
    m_runner->release();
    settle();
    // Its answer is dropped (nothing shown changes) and one more read starts.
    QCOMPARE(changes.count(), 0);
    QCOMPARE(m_runner->posted, readsBefore + 2);

    m_runner->holding = false;
    m_runner->release();
    settle();
    QCOMPARE(changes.count(), 1);
    QCOMPARE(m_runner->posted, readsBefore + 2);
}

QTEST_GUILESS_MAIN(OfflineViewModelTest)
#include "tst_offline_view_model.moc"
