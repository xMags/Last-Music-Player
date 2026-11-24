#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/db/LibraryQueries.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/QueueTypes.h"
#include "ui/menus/MenuItemListModel.h"
#include "ui/player/QueueListModel.h"
#include "ui/player/QueueRailViewModel.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::PlayerController;
using lmp::core::RepeatMode;
using lmp::core::Track;
using lmp::ui::MenuItemListModel;
using lmp::ui::QueueRailViewModel;
using lmp::ui::ToastQueue;

namespace {

QStringList menuIds(const MenuItemListModel& menu)
{
    QStringList ids;
    for (int row = 0; row < menu.count(); ++row) {
        ids.append(menu.idAt(row));
    }
    return ids;
}

} // namespace

// The Up Next view model against a real player controller (fake audio) and
// a temporary library for the recent plays.
class QueueRailViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listsTheSongsToCome();
    void playRowPlaysThatSongUnlessTheRowChanged();
    void moveRowCommitsTheDraggedOrder();
    void menuItemsFollowTheRowsPlace();
    void rowMenuOffersTheSongItems();
    void menuActionsFollowTheSongWhenTheQueueMoves();
    void removeOffersUndoThatPutsTheSongBack();
    void removeOffersNoUndoWhenTheSongIsStillQueued();
    void clearEmptiesTheList();
    void autoplayStatusSaysWhenItIsOff();
    void idlePanelListsSixRecentPlaysAndPlaysThem();

private:
    void makeViewModel();
    void playSongs(const QStringList& names);
    void recordPlays(const QStringList& names);
    [[nodiscard]] QString keyAt(int row) const;

    std::unique_ptr<PlayerFixture> m_fixture;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_library;
    std::unique_ptr<ToastQueue> m_toasts;
    std::unique_ptr<PlayerController> m_player;
    std::unique_ptr<QueueRailViewModel> m_rail;
};

void QueueRailViewModelTest::init()
{
    m_fixture = std::make_unique<PlayerFixture>();
    QVERIFY(m_fixture->ok());
    m_library = std::make_unique<lmp::core::db::LibraryQueries>(m_fixture->library.database());
    m_toasts = std::make_unique<ToastQueue>();
    m_player = m_fixture->makeController();
}

void QueueRailViewModelTest::cleanup()
{
    m_rail.reset();
    m_player.reset();
    m_toasts.reset();
    m_library.reset();
    m_fixture.reset();
}

void QueueRailViewModelTest::makeViewModel()
{
    m_rail = std::make_unique<QueueRailViewModel>(*m_player, *m_library, *m_toasts);
}

void QueueRailViewModelTest::playSongs(const QStringList& names)
{
    m_player->playInContext(m_fixture->song(names.first()), m_fixture->songs(names));
    settle();
    m_fixture->audio.start();
}

void QueueRailViewModelTest::recordPlays(const QStringList& names)
{
    // The first name is the latest play.
    quint64 order = static_cast<quint64>(names.size());
    for (const QString& name : names) {
        const Track track = m_fixture->song(name);
        const QString key = lmp::core::catalogSourceKey(track);
        QVERIFY(m_fixture->tracks.upsertLocalTrack(track, key).isOk());
        QVERIFY(m_fixture->tracks.recordPlayback(key, order--).isOk());
    }
}

QString QueueRailViewModelTest::keyAt(int row) const
{
    return m_rail->queue()->keyAt(row);
}

void QueueRailViewModelTest::listsTheSongsToCome()
{
    makeViewModel();
    QVERIFY(!m_rail->hasTrack());
    QSignalSpy track(m_rail.get(), &QueueRailViewModel::trackChanged);
    playSongs({"a", "b", "c", "d"});
    QVERIFY(m_rail->hasTrack());
    QVERIFY(track.count() >= 1);
    QCOMPARE(m_rail->queue()->count(), 3);
    QCOMPARE(m_rail->queue()->index(0).data(lmp::ui::QueueListModel::TitleRole).toString(), QStringLiteral("b"));
}

void QueueRailViewModelTest::playRowPlaysThatSongUnlessTheRowChanged()
{
    makeViewModel();
    playSongs({"a", "b", "c", "d"});
    const QString staleKey = keyAt(0);
    m_rail->playRow(1, keyAt(1));
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("c"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"d"}));

    // The row the pointer was on is no longer the song it showed.
    m_rail->playRow(0, staleKey);
    m_rail->playRow(5, keyAt(0));
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("c"));
}

void QueueRailViewModelTest::moveRowCommitsTheDraggedOrder()
{
    makeViewModel();
    playSongs({"a", "b", "c", "d", "e"});
    m_rail->moveRow(0, 2, keyAt(0));
    QCOMPARE(visibleTitles(*m_player), QStringList({"c", "d", "b", "e"}));
    m_rail->moveRow(3, 0, keyAt(3));
    QCOMPARE(visibleTitles(*m_player), QStringList({"e", "c", "d", "b"}));
    // Stale keys and impossible targets change nothing.
    m_rail->moveRow(0, 1, keyAt(1));
    m_rail->moveRow(0, 9, keyAt(0));
    QCOMPARE(visibleTitles(*m_player), QStringList({"e", "c", "d", "b"}));
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("a"));
}

void QueueRailViewModelTest::rowMenuOffersTheSongItems()
{
    lmp::test::ModeServiceFixture modes;
    lmp::core::db::CollectionRepository collections(m_fixture->library.database());
    lmp::core::db::GroupQueries groups(m_fixture->library.database());
    lmp::core::UserDataOperationGate gate;
    lmp::core::TrackActions actions(lmp::core::TrackActions::Dependencies{
        *m_player, m_fixture->tracks, collections, groups, modes.modes, gate});
    lmp::ui::TrackMenuController trackMenu(actions);
    makeViewModel();
    m_rail->setTrackMenu(&trackMenu);
    playSongs({"a", "b", "c"});

    // The queue's own actions, the song menu's items, then Remove.
    QVERIFY(m_rail->prepareMenu(1, keyAt(1)));
    const QStringList ids = menuIds(*m_rail->menu());
    QCOMPARE(ids.first(), QStringLiteral("play"));
    QCOMPARE(ids.last(), QStringLiteral("remove"));
    QVERIFY(ids.contains(QStringLiteral("song:playlist")));
    QVERIFY(ids.contains(QStringLiteral("song:like")));
    QVERIFY(ids.contains(QStringLiteral("song:artist")));
    QVERIFY(ids.indexOf(QStringLiteral("song:playlist")) > ids.indexOf(QStringLiteral("down")));
    QVERIFY(!ids.contains(QStringLiteral("song:next")));
    // Add to playlist starts a new group, as in the song menu.
    const QModelIndex playlist = m_rail->menu()->index(static_cast<int>(ids.indexOf(QStringLiteral("song:playlist"))));
    QVERIFY(playlist.data(MenuItemListModel::SeparatorBeforeRole).toBool());

    // A song item goes to the song menu, prepared for this row's song.
    QSignalSpy playlistMenu(&trackMenu, &lmp::ui::TrackMenuController::playlistMenuRequested);
    m_rail->triggerMenu(QStringLiteral("song:playlist"));
    QCOMPARE(playlistMenu.count(), 1);
}

void QueueRailViewModelTest::menuItemsFollowTheRowsPlace()
{
    makeViewModel();
    playSongs({"a", "b", "c", "d"});
    QVERIFY(m_rail->prepareMenu(0, keyAt(0)));
    QCOMPARE(menuIds(*m_rail->menu()), QStringList({"play", "down", "remove"}));
    QVERIFY(m_rail->prepareMenu(1, keyAt(1)));
    QCOMPARE(menuIds(*m_rail->menu()), QStringList({"play", "next", "up", "down", "remove"}));
    QVERIFY(m_rail->prepareMenu(2, keyAt(2)));
    QCOMPARE(menuIds(*m_rail->menu()), QStringList({"play", "next", "up", "remove"}));
    const QModelIndex remove = m_rail->menu()->index(3);
    QVERIFY(remove.data(MenuItemListModel::DangerRole).toBool());
    QVERIFY(remove.data(MenuItemListModel::SeparatorBeforeRole).toBool());
    QCOMPARE(remove.data(MenuItemListModel::LabelRole).toString(), QStringLiteral("Remove from queue"));
    QVERIFY(!m_rail->prepareMenu(1, keyAt(2)));
}

void QueueRailViewModelTest::menuActionsFollowTheSongWhenTheQueueMoves()
{
    makeViewModel();
    playSongs({"a", "b", "c", "d", "e"});
    QVERIFY(m_rail->prepareMenu(2, keyAt(2))); // "d"
    // A song ends while the menu is open: "d" is now row 1.
    m_player->next();
    settle();
    QCOMPARE(visibleTitles(*m_player), QStringList({"c", "d", "e"}));
    m_rail->triggerMenu(QStringLiteral("up"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"d", "c", "e"}));

    QVERIFY(m_rail->prepareMenu(2, keyAt(2))); // "e"
    m_rail->triggerMenu(QStringLiteral("next"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"e", "d", "c"}));
    QVERIFY(m_rail->prepareMenu(0, keyAt(0)));
    m_rail->triggerMenu(QStringLiteral("down"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"d", "e", "c"}));

    // A song that left the queue is not acted on.
    QVERIFY(m_rail->prepareMenu(1, keyAt(1))); // "e"
    m_player->removeVisibleEntry(1);
    m_rail->triggerMenu(QStringLiteral("play"));
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"d", "c"}));

    QVERIFY(m_rail->prepareMenu(1, keyAt(1))); // "c"
    m_rail->triggerMenu(QStringLiteral("play"));
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("c"));
}

void QueueRailViewModelTest::removeOffersUndoThatPutsTheSongBack()
{
    makeViewModel();
    playSongs({"a", "b", "c", "d"});
    QVERIFY(m_rail->prepareMenu(1, keyAt(1))); // "c"
    m_rail->triggerMenu(QStringLiteral("remove"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"b", "d"}));
    QCOMPARE(m_toasts->count(), 1);
    const QModelIndex toast = m_toasts->index(0);
    QCOMPARE(toast.data(ToastQueue::TextRole).toString(), QStringLiteral("Removed from queue"));
    QCOMPARE(toast.data(ToastQueue::ActionLabelRole).toString(), QStringLiteral("Undo"));

    m_toasts->triggerAction(toast.data(ToastQueue::ToastIdRole).toInt());
    QCOMPARE(visibleTitles(*m_player), QStringList({"b", "c", "d"}));
    QCOMPARE(m_toasts->count(), 0);
}

void QueueRailViewModelTest::removeOffersNoUndoWhenTheSongIsStillQueued()
{
    makeViewModel();
    playSongs({"a", "b", "c"});
    // "b" is listed twice: queued and next in the context.
    m_player->addToQueue(m_fixture->songs({"b"}));
    QCOMPARE(visibleTitles(*m_player), QStringList({"b", "b", "c"}));
    QVERIFY(m_rail->prepareMenu(0, keyAt(0)));
    m_rail->triggerMenu(QStringLiteral("remove"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"b", "c"}));
    QCOMPARE(m_toasts->count(), 1);
    QVERIFY(m_toasts->index(0).data(ToastQueue::ActionLabelRole).toString().isEmpty());
}

void QueueRailViewModelTest::clearEmptiesTheList()
{
    makeViewModel();
    playSongs({"a", "b", "c"});
    m_rail->clearUpcoming();
    QCOMPARE(m_rail->queue()->count(), 0);
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("a"));
}

void QueueRailViewModelTest::autoplayStatusSaysWhenItIsOff()
{
    makeViewModel();
    QSignalSpy status(m_rail.get(), &QueueRailViewModel::autoplayStatusChanged);
    QVERIFY(m_rail->autoplayStatus().isEmpty());
    m_player->setRepeatMode(RepeatMode::All);
    QCOMPARE(m_rail->autoplayStatus(), QStringLiteral("Autoplay off"));
    m_player->setRepeatMode(RepeatMode::Off);
    m_player->setAutoplay(false);
    QCOMPARE(m_rail->autoplayStatus(), QStringLiteral("Autoplay off"));
    QCOMPARE(status.count(), 3);
}

void QueueRailViewModelTest::idlePanelListsSixRecentPlaysAndPlaysThem()
{
    recordPlays({"c", "h", "g", "f", "e", "d", "b", "a"});
    makeViewModel();
    lmp::ui::QueueListModel* recent = m_rail->recent();
    QCOMPARE(recent->count(), QueueRailViewModel::RecentLimit);
    QStringList titles;
    for (int row = 0; row < recent->count(); ++row) {
        titles.append(recent->index(row).data(lmp::ui::QueueListModel::TitleRole).toString());
    }
    QCOMPARE(titles, QStringList({"c", "h", "g", "f", "e", "d"}));

    // The whole history follows, past the rows shown.
    m_rail->playRecent(1);
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("h"));
    QCOMPARE(visibleTitles(*m_player), QStringList({"g", "f", "e", "d", "b", "a"}));
    m_rail->playRecent(9);
    settle();
    QCOMPARE(m_player->currentTrack()->title, QStringLiteral("h"));
}

QTEST_GUILESS_MAIN(QueueRailViewModelTest)
#include "tst_queue_rail_view_model.moc"
