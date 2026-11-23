#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>

#include "../core/FakeTagReader.h"
#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/ArtworkCache.h"
#include "core/library/LibraryScanner.h"
#include "core/library/CollectionActions.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "ui/library/LibraryViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::Track;
using lmp::ui::CollectionCardModel;
using lmp::ui::LibraryViewModel;
using lmp::ui::Navigator;
using lmp::ui::Route;

// The Library page's tabs read the local library: system playlists, groups,
// History and Songs; the tab is kept per history entry and cards open pages.
class LibraryViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void emptyLibraryShowsOnlyFavourites();
    void tabsReadTheLibrary();
    void tabIsKeptInTheHistoryEntry();
    void cardsOpenCollectionPages();
    void songsSortAndFilter();
    void likingUpdatesFavourites();
    void historyFollowsPlaysAndSorts();
    void songMenuGoesToAlbum();

private:
    void addSong(const QString& title, const QString& artist, const QString& album, const QString& genre,
                 double seconds = 200.0);
    [[nodiscard]] Track storedSong(const QString& title) const;
    [[nodiscard]] static QStringList titles(const CollectionCardModel& cards);

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::TrackActions> m_actions;
    std::unique_ptr<lmp::core::CollectionActions> m_collectionActions;
    std::unique_ptr<lmp::test::FakeTagReader> m_tags;
    std::unique_ptr<QTemporaryDir> m_thumbs;
    std::unique_ptr<lmp::core::ArtworkCache> m_artwork;
    std::unique_ptr<lmp::core::LibraryScanner> m_scanner;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<Navigator> m_navigator;
    std::unique_ptr<LibraryViewModel> m_model;
    QMap<QString, Track> m_songs;
};

void LibraryViewModelTest::init()
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
    m_collectionActions = std::make_unique<lmp::core::CollectionActions>(*m_collections, *m_gate);
    m_tags = std::make_unique<lmp::test::FakeTagReader>();
    m_thumbs = std::make_unique<QTemporaryDir>();
    m_artwork = std::make_unique<lmp::core::ArtworkCache>(m_thumbs->path(), nullptr);
    m_scanner = std::make_unique<lmp::core::LibraryScanner>(lmp::core::LibraryScanner::Dependencies{
        *m_tags, *m_artwork, m_player->tracks, *m_queries, m_player->settings, *m_gate});
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_navigator = std::make_unique<Navigator>();
    m_model = std::make_unique<LibraryViewModel>(LibraryViewModel::Parts{
        *m_queries, *m_groups, *m_actions, *m_collectionActions, *m_controller, *m_scanner, *m_menu, *m_navigator});
    m_songs.clear();
}

void LibraryViewModelTest::cleanup()
{
    m_model.reset();
    m_navigator.reset();
    m_menu.reset();
    m_scanner.reset();
    m_artwork.reset();
    m_thumbs.reset();
    m_tags.reset();
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

void LibraryViewModelTest::addSong(const QString& title, const QString& artist, const QString& album,
                                   const QString& genre, double seconds)
{
    Track track = m_player->song(title);
    track.artist = artist;
    track.album = album;
    track.genre = genre;
    track.durationSeconds = seconds;
    QVERIFY(m_player->tracks.upsertLocalTrack(track, lmp::core::catalogSourceKey(track)).isOk());
    m_songs.insert(title, track);
}

Track LibraryViewModelTest::storedSong(const QString& title) const
{
    return m_songs.value(title);
}

QStringList LibraryViewModelTest::titles(const CollectionCardModel& cards)
{
    QStringList list;
    for (int row = 0; row < cards.count(); ++row) {
        list.append(cards.cardAt(row).title);
    }
    return list;
}

void LibraryViewModelTest::emptyLibraryShowsOnlyFavourites()
{
    m_model->refresh();
    QVERIFY(m_model->empty());
    // Most Played and Recently Added are left out while they have nothing.
    QCOMPARE(titles(*m_model->playlists()), QStringList{QStringLiteral("Favourites")});
    QCOMPARE(m_model->albums()->count(), 0);
    QCOMPARE(m_model->songs()->totalCount(), 0);
}

void LibraryViewModelTest::tabsReadTheLibrary()
{
    addSong(QStringLiteral("One"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"));
    addSong(QStringLiteral("Two"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"));
    addSong(QStringLiteral("Three"), QStringLiteral("Canyon"), QStringLiteral("Valleys"), QStringLiteral("Ambient"));
    QSignalSpy content(m_model.get(), &LibraryViewModel::contentChanged);
    m_model->refresh();
    QCOMPARE(content.count(), 1);
    QVERIFY(!m_model->empty());

    QCOMPARE(titles(*m_model->playlists()),
             (QStringList{QStringLiteral("Favourites"), QStringLiteral("Recently Added")}));
    QCOMPARE(m_model->playlists()->cardAt(1).subtitle, QStringLiteral("3 songs"));
    QCOMPARE(m_model->playlists()->cardAt(0).kind, QStringLiteral("system"));
    QCOMPARE(m_model->playlists()->cardAt(0).key, QStringLiteral("smart-liked"));

    QStringList albums = titles(*m_model->albums());
    albums.sort();
    QCOMPARE(albums, (QStringList{QStringLiteral("Late Hours"), QStringLiteral("Valleys")}));
    QStringList artists = titles(*m_model->artists());
    artists.sort();
    QCOMPARE(artists, (QStringList{QStringLiteral("Canyon"), QStringLiteral("Night Owls")}));
    QCOMPARE(m_model->artists()->cardAt(0).kind, QStringLiteral("artist"));
    QStringList genres = titles(*m_model->genres());
    genres.sort();
    QCOMPARE(genres, (QStringList{QStringLiteral("Ambient"), QStringLiteral("Rock")}));
    QCOMPARE(m_model->songs()->totalCount(), 3);
}

void LibraryViewModelTest::tabIsKeptInTheHistoryEntry()
{
    m_navigator->push(Route(Route::Kind::Library));
    QCOMPARE(m_model->tab(), QStringLiteral("playlists"));
    QSignalSpy tab(m_model.get(), &LibraryViewModel::tabChanged);
    m_model->setTab(QStringLiteral("albums"));
    QCOMPARE(tab.count(), 1);
    m_model->setTab(QStringLiteral("nonsense"));
    QCOMPARE(m_model->tab(), QStringLiteral("albums"));
    QCOMPARE(m_navigator->current().param(QStringLiteral("tab")), QStringLiteral("albums"));

    // A fresh Library entry starts on Playlists; Back returns to Albums.
    m_navigator->push(Route(Route::Kind::Settings));
    m_navigator->push(Route(Route::Kind::Library));
    QCOMPARE(m_model->tab(), QStringLiteral("playlists"));
    m_navigator->back();
    m_navigator->back();
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Library);
    QCOMPARE(m_model->tab(), QStringLiteral("albums"));
}

void LibraryViewModelTest::cardsOpenCollectionPages()
{
    addSong(QStringLiteral("One"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"));
    m_navigator->push(Route(Route::Kind::Library));
    m_model->openCard(QStringLiteral("genres"), 0);
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("genre"));
    QCOMPARE(m_navigator->current().param(QStringLiteral("title")), QStringLiteral("Rock"));
    QVERIFY(!m_navigator->current().param(QStringLiteral("key")).isEmpty());

    m_navigator->back();
    m_model->openCard(QStringLiteral("playlists"), 0);
    QCOMPARE(m_navigator->current().param(QStringLiteral("key")), QStringLiteral("smart-liked"));

    // Rows out of range and unknown tabs do nothing.
    m_navigator->back();
    m_model->openCard(QStringLiteral("albums"), 9);
    m_model->openCard(QStringLiteral("songs"), 0);
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Library);
}

void LibraryViewModelTest::songsSortAndFilter()
{
    addSong(QStringLiteral("Bravo"), QStringLiteral("A"), QStringLiteral("X"), QString(), 300);
    addSong(QStringLiteral("Alpha"), QStringLiteral("B"), QStringLiteral("X"), QString(), 100);
    addSong(QStringLiteral("Charlie"), QStringLiteral("C"), QStringLiteral("Y"), QString(), 200);
    m_model->refresh();

    m_model->setSongsSort(1);
    lmp::ui::TrackListModel* songs = m_model->songs();
    QCOMPARE(songs->trackAt(0).title, QStringLiteral("Alpha"));
    QCOMPARE(songs->trackAt(2).title, QStringLiteral("Charlie"));
    m_model->setSongsSort(99);
    QCOMPARE(m_model->songsSort(), 1);

    m_model->setSongsFilter(QStringLiteral("char"));
    QCOMPARE(songs->totalCount(), 1);
    QCOMPARE(songs->trackAt(0).title, QStringLiteral("Charlie"));
    // A filter with no match is not an empty library.
    m_model->setSongsFilter(QStringLiteral("zzz"));
    m_model->refresh();
    QCOMPARE(songs->totalCount(), 0);
    QVERIFY(!m_model->empty());
}

void LibraryViewModelTest::likingUpdatesFavourites()
{
    addSong(QStringLiteral("One"), QStringLiteral("A"), QStringLiteral("X"), QString());
    m_model->refresh();
    QCOMPARE(m_model->playlists()->cardAt(0).subtitle, QStringLiteral("0 songs"));
    m_model->likeSong(QStringLiteral("songs"), 0);
    QCOMPARE(m_model->playlists()->cardAt(0).subtitle, QStringLiteral("1 song"));
    QVERIFY(m_model->songs()->trackAt(0).liked);
}

void LibraryViewModelTest::historyFollowsPlaysAndSorts()
{
    addSong(QStringLiteral("Bravo"), QStringLiteral("A"), QStringLiteral("X"), QString());
    addSong(QStringLiteral("Alpha"), QStringLiteral("B"), QStringLiteral("X"), QString());
    QVERIFY(m_player->tracks.recordPlayback(lmp::core::catalogSourceKey(storedSong(QStringLiteral("Alpha"))), 1).isOk());
    QVERIFY(m_player->tracks.recordPlayback(lmp::core::catalogSourceKey(storedSong(QStringLiteral("Bravo"))), 2).isOk());
    m_model->refresh();
    QCOMPARE(m_model->history()->count(), 2);
    // Most recent first, then by title.
    QCOMPARE(m_model->history()->trackAt(0).title, QStringLiteral("Bravo"));
    m_model->setHistorySort(1);
    QCOMPARE(m_model->history()->trackAt(0).title, QStringLiteral("Alpha"));
    // Played songs make Most Played appear.
    QVERIFY(titles(*m_model->playlists()).contains(QStringLiteral("Most Played")));

    m_model->playSong(QStringLiteral("history"), 1);
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("Bravo"));
}

void LibraryViewModelTest::songMenuGoesToAlbum()
{
    addSong(QStringLiteral("One"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"));
    m_model->refresh();
    m_navigator->push(Route(Route::Kind::Library));
    emit m_actions->albumRequested(m_model->songs()->trackAt(0));
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("album"));
    QCOMPARE(m_navigator->current().param(QStringLiteral("title")), QStringLiteral("Late Hours"));
}

QTEST_MAIN(LibraryViewModelTest)
#include "tst_library_view_model.moc"
