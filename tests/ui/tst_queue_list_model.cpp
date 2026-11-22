#include <QAbstractItemModelTester>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <memory>

#include "../core/PlayerFixture.h"
#include "core/playback/QueueTypes.h"
#include "ui/player/QueueListDiff.h"
#include "ui/player/QueueListModel.h"

using lmp::core::PlayerController;
using lmp::core::QueueDirection;
using lmp::ui::QueueListDiff;
using lmp::ui::QueueListModel;

namespace {

using Kind = QueueListDiff::Kind;
using Run = QueueListDiff::Run;

QStringList keys(const char* letters)
{
    QStringList list;
    for (const char* letter = letters; *letter != '\0'; ++letter) {
        list.append(QString(QLatin1Char(*letter)));
    }
    return list;
}

} // namespace

// The Up Next model: the diff between two key lists, and the notifications
// the model sends for real queue edits made through a player controller.
class QueueListModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void diffFindsNothingForTheSameList();
    void diffFindsASingleMoveEitherWay();
    void diffSplicesRemovalsAndInsertionsInOrder();
    void diffResetsForAmbiguousOrLargeChanges();
    void rowsShowTheSong();
    void repeatedSongsGetTheirOwnKeys();
    void aMoveIsOneMoveNotification();
    void removeAndRestoreAreRowNotifications();
    void theNextSongStartingRemovesTheFirstRow();
    void aMixedChangeResets();
    void aRowThatChangesGroupGetsDataChanged();

private:
    void playSongs(const QStringList& names);
    void sync();

    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<PlayerController> m_player;
    std::unique_ptr<QueueListModel> m_model;
    std::unique_ptr<QAbstractItemModelTester> m_tester;
};

void QueueListModelTest::init()
{
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_player = m_fixture->makeController();
    m_model = std::make_unique<QueueListModel>();
    m_tester = std::make_unique<QAbstractItemModelTester>(m_model.get(),
                                                          QAbstractItemModelTester::FailureReportingMode::QtTest);
}

void QueueListModelTest::cleanup()
{
    m_tester.reset();
    m_model.reset();
    m_player.reset();
    m_fixture.reset();
}

void QueueListModelTest::playSongs(const QStringList& names)
{
    m_player->playInContext(m_fixture->song(names.first()), m_fixture->songs(names));
    settle();
    m_fixture->audio.start();
    sync();
}

void QueueListModelTest::sync()
{
    m_model->setEntries(m_player->visibleQueue());
}

void QueueListModelTest::diffFindsNothingForTheSameList()
{
    QCOMPARE(QueueListDiff::between(keys("abc"), keys("abc")).kind, Kind::None);
    QCOMPARE(QueueListDiff::between({}, {}).kind, Kind::None);
}

void QueueListModelTest::diffFindsASingleMoveEitherWay()
{
    QueueListDiff down = QueueListDiff::between(keys("abcde"), keys("bcdae"));
    QCOMPARE(down.kind, Kind::Move);
    QCOMPARE(down.from, 0);
    QCOMPARE(down.to, 3);

    QueueListDiff up = QueueListDiff::between(keys("abcde"), keys("aebcd"));
    QCOMPARE(up.kind, Kind::Move);
    QCOMPARE(up.from, 4);
    QCOMPARE(up.to, 1);

    // Neighbours trading places read as the first moving down by one.
    QueueListDiff swap = QueueListDiff::between(keys("abc"), keys("bac"));
    QCOMPARE(swap.kind, Kind::Move);
    QCOMPARE(swap.from, 0);
    QCOMPARE(swap.to, 1);
}

void QueueListModelTest::diffSplicesRemovalsAndInsertionsInOrder()
{
    QueueListDiff removed = QueueListDiff::between(keys("abcdef"), keys("adf"));
    QCOMPARE(removed.kind, Kind::Splice);
    QCOMPARE(removed.removed, (QList<Run>{{1, 2}, {4, 4}}));
    QVERIFY(removed.inserted.isEmpty());

    QueueListDiff inserted = QueueListDiff::between(keys("ad"), keys("abcdx"));
    QCOMPARE(inserted.kind, Kind::Splice);
    QVERIFY(inserted.removed.isEmpty());
    QCOMPARE(inserted.inserted, (QList<Run>{{1, 2}, {4, 4}}));

    // The next song starting and a song added at the end.
    QueueListDiff both = QueueListDiff::between(keys("abc"), keys("bcx"));
    QCOMPARE(both.kind, Kind::Splice);
    QCOMPARE(both.removed, (QList<Run>{{0, 0}}));
    QCOMPARE(both.inserted, (QList<Run>{{2, 2}}));

    QCOMPARE(QueueListDiff::between(keys("ab"), {}).removed, (QList<Run>{{0, 1}}));
}

void QueueListModelTest::diffResetsForAmbiguousOrLargeChanges()
{
    // Two rows moved.
    QCOMPARE(QueueListDiff::between(keys("abcde"), keys("baedc")).kind, Kind::Reset);
    // A move together with a removal.
    QCOMPARE(QueueListDiff::between(keys("abcd"), keys("dba")).kind, Kind::Reset);
    // A move together with an insertion.
    QCOMPARE(QueueListDiff::between(keys("abc"), keys("cxab")).kind, Kind::Reset);
    // More runs than MaxRuns: every other row removed.
    QCOMPARE(QueueListDiff::between(keys("abcdefghijklmnopqrs"), keys("acegikmoqs")).kind, Kind::Reset);
    // An empty list filling up has nothing to animate from.
    QCOMPARE(QueueListDiff::between({}, keys("ab")).kind, Kind::Reset);
    // Exactly MaxRuns runs is still precise.
    QCOMPARE(QueueListDiff::between(keys("abcdefghijklmnopq"), keys("acegikmoq")).kind, Kind::Splice);
}

void QueueListModelTest::rowsShowTheSong()
{
    playSongs({"a", "b"});
    m_player->addToQueue(m_fixture->songs({"x"}));
    sync();
    QCOMPARE(m_model->count(), 2);
    const QModelIndex queued = m_model->index(0);
    QCOMPARE(queued.data(QueueListModel::TitleRole).toString(), QStringLiteral("x"));
    QCOMPARE(queued.data(QueueListModel::ArtistLineRole).toString(), QStringLiteral("Artist · Queued"));
    QVERIFY(queued.data(QueueListModel::QueuedRole).toBool());
    QCOMPARE(queued.data(QueueListModel::DurationTextRole).toString(), QStringLiteral("3:20"));
    QCOMPARE(queued.data(QueueListModel::ArtworkLabelRole).toString(), QStringLiteral("x"));
    QVERIFY(queued.data(QueueListModel::RowKeyRole).toString().endsWith(QStringLiteral("x.mp3#0")));
    // A local file without a picture address shows its embedded cover.
    QVERIFY(queued.data(QueueListModel::ArtworkUrlRole).toUrl().toString().startsWith(QStringLiteral("image://")));

    const QModelIndex context = m_model->index(1);
    QCOMPARE(context.data(QueueListModel::ArtistLineRole).toString(), QStringLiteral("Artist"));
    QVERIFY(!context.data(QueueListModel::QueuedRole).toBool());
}

void QueueListModelTest::repeatedSongsGetTheirOwnKeys()
{
    playSongs({"a", "b", "c"});
    // "b" is queued while it is also next in the context: listed twice.
    m_player->addToQueue(m_fixture->songs({"b"}));
    sync();
    QCOMPARE(visibleTitles(*m_player), QStringList({"b", "b", "c"}));
    const QStringList rowKeys = m_model->keys();
    QVERIFY(rowKeys.at(0).endsWith(QStringLiteral("#0")));
    QVERIFY(rowKeys.at(1).endsWith(QStringLiteral("#1")));
    QCOMPARE(rowKeys.at(0).chopped(2), rowKeys.at(1).chopped(2));
}

void QueueListModelTest::aMoveIsOneMoveNotification()
{
    playSongs({"a", "b", "c", "d", "e"});
    QSignalSpy moved(m_model.get(), &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(m_model.get(), &QAbstractItemModel::modelReset);
    QSignalSpy removed(m_model.get(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(m_model.get(), &QAbstractItemModel::rowsInserted);

    // A drag of "b" (row 0) down to row 2.
    m_player->commitVisibleOrder(m_fixture->songs({"c", "d", "b", "e"}));
    sync();
    QCOMPARE(moved.count(), 1);
    QCOMPARE(moved.at(0).at(1).toInt(), 0);
    QCOMPARE(moved.at(0).at(2).toInt(), 0);
    QCOMPARE(moved.at(0).at(4).toInt(), 3);
    QCOMPARE(m_model->index(2).data(QueueListModel::TitleRole).toString(), QStringLiteral("b"));

    // "Move up" on the last row: two neighbours trade places, which the
    // model reports as the upper one moving down past the other.
    m_player->moveVisibleEntry(3, QueueDirection::Previous);
    sync();
    QCOMPARE(moved.count(), 2);
    QCOMPARE(moved.at(1).at(1).toInt(), 2);
    QCOMPARE(moved.at(1).at(4).toInt(), 4);
    QCOMPARE(visibleTitles(*m_player), QStringList({"c", "d", "e", "b"}));
    QCOMPARE(m_model->index(2).data(QueueListModel::TitleRole).toString(), QStringLiteral("e"));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(inserted.count(), 0);
}

void QueueListModelTest::removeAndRestoreAreRowNotifications()
{
    playSongs({"a", "b", "c", "d"});
    QSignalSpy removed(m_model.get(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(m_model.get(), &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(m_model.get(), &QAbstractItemModel::modelReset);

    const lmp::core::QueueEntry entry = m_player->visibleQueue().at(1);
    m_player->removeVisibleEntry(1);
    sync();
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 1);
    QCOMPARE(removed.at(0).at(2).toInt(), 1);

    m_player->restoreVisibleEntry(1, entry);
    sync();
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 1);
    QCOMPARE(inserted.at(0).at(2).toInt(), 1);
    QCOMPARE(m_model->index(1).data(QueueListModel::TitleRole).toString(), QStringLiteral("c"));

    // Clear removes every row in one run.
    m_player->clearUpcoming();
    sync();
    QCOMPARE(removed.count(), 2);
    QCOMPARE(removed.at(1).at(1).toInt(), 0);
    QCOMPARE(removed.at(1).at(2).toInt(), 2);
    QCOMPARE(m_model->count(), 0);
    QCOMPARE(reset.count(), 0);
}

void QueueListModelTest::theNextSongStartingRemovesTheFirstRow()
{
    playSongs({"a", "b", "c"});
    QSignalSpy removed(m_model.get(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy count(m_model.get(), &QueueListModel::countChanged);
    m_player->next();
    settle();
    sync();
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 0);
    QCOMPARE(m_model->count(), 1);
    QCOMPARE(count.count(), 1);
}

void QueueListModelTest::aMixedChangeResets()
{
    playSongs({"a", "b", "c", "d", "e"});
    QSignalSpy reset(m_model.get(), &QAbstractItemModel::modelReset);
    // "e" moved to the front while "c" left: neither a move nor a splice.
    m_player->commitVisibleOrder(m_fixture->songs({"e", "b", "d"}));
    sync();
    QCOMPARE(reset.count(), 1);
    QCOMPARE(m_model->keys(), QueueListModel::keysOf(m_player->visibleQueue()));
}

void QueueListModelTest::aRowThatChangesGroupGetsDataChanged()
{
    playSongs({"a", "b", "c"});
    m_player->addToQueue(m_fixture->songs({"x"}));
    sync();
    QSignalSpy moved(m_model.get(), &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(m_model.get(), &QAbstractItemModel::dataChanged);
    // "Play next" on "b": it takes the user queue's place, "x" follows in
    // the context.
    m_player->commitVisibleOrder(m_fixture->songs({"b", "x", "c"}));
    sync();
    QCOMPARE(moved.count(), 1);
    QCOMPARE(changed.count(), 2);
    QVERIFY(m_model->index(0).data(QueueListModel::QueuedRole).toBool());
    QVERIFY(!m_model->index(1).data(QueueListModel::QueuedRole).toBool());
}

QTEST_GUILESS_MAIN(QueueListModelTest)
#include "tst_queue_list_model.moc"
