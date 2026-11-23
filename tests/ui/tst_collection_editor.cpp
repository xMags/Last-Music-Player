#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/CollectionActions.h"
#include "core/library/HomeMixService.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "ui/library/CollectionEditor.h"
#include "ui/library/CollectionViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/shell/SidebarViewModel.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuBuilder.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::CollectionActions;
using lmp::core::Track;
using lmp::ui::CollectionEditor;
using lmp::ui::Navigator;
using lmp::ui::Route;

// Making, renaming and deleting playlists and albums: the core actions, the
// prompts and confirmations, the sidebar list and the open page following.
class CollectionEditorTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void namesAreCleaned();
    void createOpensTheNewPlaylist();
    void renameUpdatesSidebarAndPage();
    void deleteLeavesThePage();
    void albumsAreMadeAndDeleted();
    void closedGateRefusesAndKeepsThePrompt();
    void sidebarListsAtMostFifty();
    void songMenuMovesWithinThePlaylist();

private:
    [[nodiscard]] QString lastToast() const;
    [[nodiscard]] QStringList sidebarNames() const;
    [[nodiscard]] Track stored(const QString& title);

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::TrackActions> m_actions;
    std::unique_ptr<CollectionActions> m_collectionActions;
    std::unique_ptr<lmp::core::HomeMixService> m_mixes;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<Navigator> m_navigator;
    std::unique_ptr<lmp::ui::ToastQueue> m_toasts;
    std::unique_ptr<lmp::ui::SidebarViewModel> m_sidebar;
    std::unique_ptr<lmp::ui::CollectionViewModel> m_page;
    std::unique_ptr<CollectionEditor> m_editor;
};

void CollectionEditorTest::init()
{
    m_player = std::make_unique<PlayerFixture>();
    QVERIFY(m_player->ok());
    m_modes = std::make_unique<lmp::test::ModeServiceFixture>();
    (void)m_modes->modes.start();
    m_controller = m_player->makeController();
    lmp::core::db::Database& database = m_player->library.database();
    m_queries = std::make_unique<lmp::core::db::LibraryQueries>(database);
    m_collections = std::make_unique<lmp::core::db::CollectionRepository>(database);
    m_groups = std::make_unique<lmp::core::db::GroupQueries>(database);
    m_gate = std::make_unique<lmp::core::UserDataOperationGate>();
    m_actions = std::make_unique<lmp::core::TrackActions>(lmp::core::TrackActions::Dependencies{
        *m_controller, m_player->tracks, *m_collections, *m_groups, m_modes->modes, *m_gate});
    m_collectionActions = std::make_unique<CollectionActions>(*m_collections, *m_gate);
    m_mixes = std::make_unique<lmp::core::HomeMixService>(*m_queries, *m_groups, [] { return QDate(2026, 11, 23); });
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_navigator = std::make_unique<Navigator>();
    m_toasts = std::make_unique<lmp::ui::ToastQueue>();
    m_sidebar = std::make_unique<lmp::ui::SidebarViewModel>(
        lmp::ui::SidebarViewModel::Parts{*m_navigator, *m_collectionActions, *m_actions});
    m_page = std::make_unique<lmp::ui::CollectionViewModel>(lmp::ui::CollectionViewModel::Parts{
        *m_queries, *m_actions, *m_collectionActions, *m_mixes, *m_controller, m_player->settings, *m_menu, *m_navigator});
    m_editor = std::make_unique<CollectionEditor>(*m_collectionActions, *m_navigator, *m_toasts);

    for (const QString& title : {QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie")}) {
        Track track = m_player->song(title);
        QVERIFY(m_player->tracks.upsertLocalTrack(track, lmp::core::catalogSourceKey(track)).isOk());
    }
}

void CollectionEditorTest::cleanup()
{
    m_editor.reset();
    m_page.reset();
    m_sidebar.reset();
    m_toasts.reset();
    m_navigator.reset();
    m_menu.reset();
    m_mixes.reset();
    m_collectionActions.reset();
    m_actions.reset();
    m_gate.reset();
    m_groups.reset();
    m_collections.reset();
    m_queries.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

// The newest toast comes first in the queue.
QString CollectionEditorTest::lastToast() const
{
    return m_toasts->count() == 0 ? QString()
                                  : m_toasts->data(m_toasts->index(0), lmp::ui::ToastQueue::TextRole).toString();
}

QStringList CollectionEditorTest::sidebarNames() const
{
    QStringList names;
    lmp::ui::SidebarPlaylistListModel* model = m_sidebar->playlists();
    for (int row = 0; row < model->rowCount(); ++row) {
        names.append(model->index(row).data(lmp::ui::SidebarPlaylistListModel::NameRole).toString());
    }
    return names;
}

Track CollectionEditorTest::stored(const QString& title)
{
    lmp::core::db::TrackQuery query;
    query.searchText = title;
    const QList<Track> found = m_queries->loadTracksForQuery(query);
    return found.isEmpty() ? Track{} : found.constFirst();
}

void CollectionEditorTest::namesAreCleaned()
{
    QCOMPARE(CollectionActions::cleanName(QStringLiteral("  Road   trip "), QStringLiteral("x")), QStringLiteral("Road trip"));
    QCOMPARE(CollectionActions::cleanName(QStringLiteral("   "), QStringLiteral("Untitled Playlist")),
             QStringLiteral("Untitled Playlist"));
    QCOMPARE(CollectionActions::cleanName(QString(100, QLatin1Char('a')), QString()).size(),
             CollectionActions::MaximumNameLength);
}

void CollectionEditorTest::createOpensTheNewPlaylist()
{
    QSignalSpy prompts(m_editor.get(), &CollectionEditor::promptRequested);
    m_editor->requestCreatePlaylist();
    QCOMPARE(prompts.count(), 1);
    QCOMPARE(m_editor->promptTitle(), QStringLiteral("Create playlist"));
    QVERIFY(m_editor->promptValue().isEmpty());

    QVERIFY(m_editor->submitPrompt(QStringLiteral(" Night drive ")));
    QCOMPARE(lastToast(), QStringLiteral("Created “Night drive”"));
    QCOMPARE(sidebarNames(), QStringList{QStringLiteral("Night drive")});
    // The new playlist's page opens.
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    QCOMPARE(m_page->title(), QStringLiteral("Night drive"));
    QVERIFY(m_page->userPlaylist());
    QCOMPARE(m_page->songCount(), 0);

    // A blank name is a fresh untitled playlist; the prompt is spent after use.
    m_editor->requestCreatePlaylist();
    QVERIFY(m_editor->submitPrompt(QString()));
    QVERIFY(sidebarNames().contains(QStringLiteral("Untitled Playlist")));
    QVERIFY(!m_editor->submitPrompt(QStringLiteral("again")));
}

void CollectionEditorTest::renameUpdatesSidebarAndPage()
{
    m_editor->requestCreatePlaylist();
    QVERIFY(m_editor->submitPrompt(QStringLiteral("Old")));
    const QString key = m_page->key();
    m_editor->requestRename(key, QStringLiteral("Old"));
    QCOMPARE(m_editor->promptValue(), QStringLiteral("Old"));
    QCOMPARE(m_editor->promptSubmit(), QStringLiteral("Rename"));
    QVERIFY(m_editor->submitPrompt(QStringLiteral("New name")));
    QCOMPARE(sidebarNames(), QStringList{QStringLiteral("New name")});
    QCOMPARE(m_page->title(), QStringLiteral("New name"));
    QCOMPARE(m_navigator->current().param(QStringLiteral("title")), QStringLiteral("New name"));
    QCOMPARE(lastToast(), QStringLiteral("Renamed to “New name”"));

    // The same name changes nothing and says nothing.
    const int toasts = m_toasts->count();
    m_editor->requestRename(key, QStringLiteral("New name"));
    QVERIFY(m_editor->submitPrompt(QStringLiteral("New name")));
    QCOMPARE(m_toasts->count(), toasts);
}

void CollectionEditorTest::deleteLeavesThePage()
{
    m_navigator->push(Route(Route::Kind::Library));
    m_editor->requestCreatePlaylist();
    QVERIFY(m_editor->submitPrompt(QStringLiteral("Gone")));
    const QString key = m_page->key();
    QSignalSpy confirms(m_editor.get(), &CollectionEditor::confirmRequested);
    m_editor->requestDelete(QStringLiteral("playlist"), key, QStringLiteral("Gone"));
    QCOMPARE(confirms.count(), 1);
    QCOMPARE(m_editor->confirmTitle(), QStringLiteral("Delete playlist?"));
    m_editor->confirm();
    QVERIFY(sidebarNames().isEmpty());
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Library);
    QCOMPARE(lastToast(), QStringLiteral("Deleted “Gone”"));

    // Unknown kinds are not asked about.
    m_editor->requestDelete(QStringLiteral("genre"), QStringLiteral("Rock"), QStringLiteral("Rock"));
    QCOMPARE(confirms.count(), 1);
    // A confirmation with nothing pending does nothing.
    m_editor->confirm();
}

void CollectionEditorTest::albumsAreMadeAndDeleted()
{
    QSignalSpy albums(m_collectionActions.get(), &CollectionActions::albumsChanged);
    m_editor->requestCreateAlbum();
    QCOMPARE(m_editor->promptTitle(), QStringLiteral("Create album"));
    QVERIFY(m_editor->submitPrompt(QStringLiteral("Mine")));
    QCOMPARE(albums.count(), 1);
    QString key;
    for (const Track& album : m_groups->loadAlbums()) {
        if (album.title == QLatin1StringView("Mine")) {
            QCOMPARE(album.sourceKind, QStringLiteral("album-collection"));
            key = album.sourceUrl;
        }
    }
    QVERIFY(!key.isEmpty());
    m_editor->requestDelete(QStringLiteral("album-collection"), key, QStringLiteral("Mine"));
    QCOMPARE(m_editor->confirmTitle(), QStringLiteral("Delete album?"));
    m_editor->confirm();
    QCOMPARE(albums.count(), 2);
    for (const Track& album : m_groups->loadAlbums()) {
        QVERIFY(album.title != QLatin1StringView("Mine"));
    }
}

void CollectionEditorTest::closedGateRefusesAndKeepsThePrompt()
{
    // A cleanup is running.
    QVERIFY(m_gate->closeAdmissions());
    m_editor->requestCreatePlaylist();
    QVERIFY(!m_editor->submitPrompt(QStringLiteral("Blocked")));
    QCOMPARE(lastToast(), QStringLiteral("Could not create the playlist."));
    QVERIFY(sidebarNames().isEmpty());
    QVERIFY(!m_collectionActions->createAlbum(QStringLiteral("x")));
    // Once the cleanup is over, the same prompt can try again.
    m_gate->reopen();
    QVERIFY(m_editor->submitPrompt(QStringLiteral("Blocked")));
    QCOMPARE(sidebarNames(), QStringList{QStringLiteral("Blocked")});
}

void CollectionEditorTest::sidebarListsAtMostFifty()
{
    for (int index = 0; index < 55; ++index) {
        QVERIFY(m_collectionActions->createPlaylist(QStringLiteral("P%1").arg(index)));
    }
    QCOMPARE(m_sidebar->playlists()->count(), lmp::ui::SidebarViewModel::PlaylistLimit);
    m_sidebar->openPlaylist(0);
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    m_sidebar->openAllPlaylists();
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Library);
    QCOMPARE(m_navigator->current().param(QStringLiteral("tab")), QStringLiteral("playlists"));
    m_sidebar->openLiked();
    QCOMPARE(m_navigator->current().param(QStringLiteral("key")), QStringLiteral("smart-liked"));
}

void CollectionEditorTest::songMenuMovesWithinThePlaylist()
{
    const lmp::core::Result<QString> key = m_actions->addToNewPlaylist(stored(QStringLiteral("Alpha")), QStringLiteral("Mix"));
    QVERIFY(key);
    QVERIFY(m_actions->addToPlaylist(stored(QStringLiteral("Bravo")), key.value()));
    QVERIFY(m_actions->addToPlaylist(stored(QStringLiteral("Charlie")), key.value()));
    m_page->open(QStringLiteral("playlist"), key.value(), QStringLiteral("Mix"));

    auto ids = [this] {
        QStringList list;
        for (int row = 0; row < m_menu->menu()->rowCount(); ++row) {
            list.append(m_menu->menu()->idAt(row));
        }
        return list;
    };
    m_page->prepareMenu(0);
    QVERIFY(!ids().contains(QString(lmp::ui::TrackMenuBuilder::MoveUp)));
    QVERIFY(ids().contains(QString(lmp::ui::TrackMenuBuilder::MoveDown)));
    m_menu->trigger(QString(lmp::ui::TrackMenuBuilder::MoveDown));
    QCOMPARE(m_page->tracks()->trackAt(1).title, QStringLiteral("Alpha"));

    m_page->prepareMenu(2);
    QVERIFY(ids().contains(QString(lmp::ui::TrackMenuBuilder::MoveUp)));
    QVERIFY(!ids().contains(QString(lmp::ui::TrackMenuBuilder::MoveDown)));
    m_menu->trigger(QString(lmp::ui::TrackMenuBuilder::MoveUp));
    QCOMPARE(m_page->tracks()->trackAt(1).title, QStringLiteral("Charlie"));

    // Not while the page shows another order.
    m_page->setSort(static_cast<int>(m_page->sorts().indexOf(QStringLiteral("Title"))));
    m_page->prepareMenu(0);
    QVERIFY(!ids().contains(QString(lmp::ui::TrackMenuBuilder::MoveDown)));
}

QTEST_MAIN(CollectionEditorTest)
#include "tst_collection_editor.moc"
