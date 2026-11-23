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
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "core/settings/SettingKeys.h"
#include "ui/library/CollectionViewModel.h"
#include "ui/library/LocalArtistViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::Track;
using lmp::ui::CollectionViewModel;
using lmp::ui::LocalArtistViewModel;
using lmp::ui::Navigator;
using lmp::ui::Route;

// The collection pages: an album, a genre, a system playlist and a user
// playlist with find, sort, density, selection, move, remove and reorder;
// and the local artist page.
class CollectionViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void albumShowsItsSongsAndHeader();
    void genreOffersNoCustomOrder();
    void systemPlaylistFollowsLikes();
    void findAndSortNarrowAndOrder();
    void densityIsSaved();
    void selectionTogglesExtendsAndQueues();
    void selectionMovesAndRemoves();
    void dragStoresTheNewOrder();
    void routeOpensThePage();
    void artistGroupsAlbumsAndTopSongs();

private:
    void addSong(const QString& title, const QString& artist, const QString& album, const QString& genre,
                 double seconds = 200.0);
    // A user playlist holding these songs, in order; its key.
    [[nodiscard]] QString makePlaylist(const QString& name, const QStringList& titles);
    [[nodiscard]] Track stored(const QString& title);
    [[nodiscard]] QStringList titles() const;
    [[nodiscard]] QStringList playlistTitles(const QString& key);

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::TrackActions> m_actions;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<Navigator> m_navigator;
    std::unique_ptr<CollectionViewModel> m_model;
    std::unique_ptr<LocalArtistViewModel> m_artist;
};

void CollectionViewModelTest::init()
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
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_navigator = std::make_unique<Navigator>();
    m_model = std::make_unique<CollectionViewModel>(CollectionViewModel::Parts{
        *m_queries, *m_actions, *m_controller, m_player->settings, *m_menu, *m_navigator});
    m_artist = std::make_unique<LocalArtistViewModel>(
        LocalArtistViewModel::Parts{*m_queries, *m_actions, *m_controller, *m_menu, *m_navigator});

    addSong(QStringLiteral("Bravo"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"), 300);
    addSong(QStringLiteral("Alpha"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"), 100);
    addSong(QStringLiteral("Charlie"), QStringLiteral("Night Owls"), QStringLiteral("City Lights"), QStringLiteral("Rock"), 200);
    addSong(QStringLiteral("Delta"), QStringLiteral("Canyon"), QStringLiteral("Valleys"), QStringLiteral("Ambient"), 3700);
    addSong(QStringLiteral("Echo"), QStringLiteral("Night Owls"), QStringLiteral("Unknown Album"), QStringLiteral("Rock"), 60);
}

void CollectionViewModelTest::cleanup()
{
    m_artist.reset();
    m_model.reset();
    m_navigator.reset();
    m_menu.reset();
    m_actions.reset();
    m_gate.reset();
    m_groups.reset();
    m_collections.reset();
    m_queries.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

void CollectionViewModelTest::addSong(const QString& title, const QString& artist, const QString& album,
                                      const QString& genre, double seconds)
{
    Track track = m_player->song(title);
    track.artist = artist;
    track.album = album;
    track.genre = genre;
    track.durationSeconds = seconds;
    QVERIFY(m_player->tracks.upsertLocalTrack(track, lmp::core::catalogSourceKey(track)).isOk());
}

Track CollectionViewModelTest::stored(const QString& title)
{
    lmp::core::db::TrackQuery query;
    query.searchText = title;
    const QList<Track> found = m_queries->loadTracksForQuery(query);
    for (const Track& track : found) {
        if (track.title == title) {
            return track;
        }
    }
    return {};
}

QString CollectionViewModelTest::makePlaylist(const QString& name, const QStringList& titles)
{
    const lmp::core::Result<QString> key = m_actions->addToNewPlaylist(stored(titles.constFirst()), name);
    if (!key) {
        return {};
    }
    for (qsizetype index = 1; index < titles.size(); ++index) {
        if (!m_actions->addToPlaylist(stored(titles.at(index)), key.value())) {
            return {};
        }
    }
    return key.value();
}

QStringList CollectionViewModelTest::titles() const
{
    QStringList list;
    for (const Track& track : m_model->tracks()->tracks()) {
        list.append(track.title);
    }
    return list;
}

QStringList CollectionViewModelTest::playlistTitles(const QString& key)
{
    // An empty sort reads the playlist's stored order.
    lmp::core::db::TrackQuery query;
    query.groupKind = QStringLiteral("playlist");
    query.groupKey = key;
    query.sort.clear();
    QStringList list;
    for (const Track& track : m_queries->loadTracksForQuery(query)) {
        list.append(track.title);
    }
    return list;
}

void CollectionViewModelTest::albumShowsItsSongsAndHeader()
{
    m_model->open(QStringLiteral("album"), QStringLiteral("Late Hours"), QStringLiteral("Late Hours"));
    QCOMPARE(m_model->songCount(), 2);
    QCOMPARE(m_model->eyebrow(), QStringLiteral("ALBUM"));
    QCOMPARE(m_model->backLabel(), QStringLiteral("All albums"));
    QCOMPARE(m_model->byline(), QStringLiteral("Night Owls"));
    // 400 seconds: 7 minutes.
    QCOMPARE(m_model->info(), QStringLiteral("2 songs · 7 min"));
    QCOMPARE(m_model->coverStyle(), QStringLiteral("image"));
    QVERIFY(!m_model->userPlaylist());
    QVERIFY(!m_model->canReorder());
    QCOMPARE(m_model->artworkLabel().isEmpty(), false);
}

void CollectionViewModelTest::genreOffersNoCustomOrder()
{
    m_model->open(QStringLiteral("genre"), QStringLiteral("Ambient"), QStringLiteral("Ambient"));
    QCOMPARE(m_model->coverStyle(), QStringLiteral("genre"));
    QCOMPARE(m_model->eyebrow(), QStringLiteral("GENRE"));
    QVERIFY(!m_model->sorts().contains(QStringLiteral("Custom order")));
    QCOMPARE(m_model->sorts().at(m_model->sort()), QStringLiteral("Recently added"));
    // An hour and a bit.
    QCOMPARE(m_model->info(), QStringLiteral("1 song · 1 hr 2 min"));
}

void CollectionViewModelTest::systemPlaylistFollowsLikes()
{
    m_model->open(QStringLiteral("system"), QStringLiteral("smart-liked"), QString());
    QCOMPARE(m_model->title(), QStringLiteral("Favourites"));
    QCOMPARE(m_model->coverStyle(), QStringLiteral("system"));
    QCOMPARE(m_model->iconName(), QStringLiteral("heart-fill"));
    QVERIFY(!m_model->description().isEmpty());
    QCOMPARE(m_model->songCount(), 0);
    QVERIFY(m_model->emptyText().contains(QStringLiteral("heart")));

    QVERIFY(m_actions->toggleLike(stored(QStringLiteral("Alpha"))).value_or(false));
    QCOMPARE(m_model->songCount(), 1);
    QCOMPARE(titles(), QStringList{QStringLiteral("Alpha")});
}

void CollectionViewModelTest::findAndSortNarrowAndOrder()
{
    const QString key = makePlaylist(QStringLiteral("Mix"),
                                     {QStringLiteral("Charlie"), QStringLiteral("Alpha"), QStringLiteral("Bravo")});
    QVERIFY(!key.isEmpty());
    m_model->open(QStringLiteral("playlist"), key, QStringLiteral("Mix"));
    QVERIFY(m_model->userPlaylist());
    QCOMPARE(m_model->sorts().at(m_model->sort()), QStringLiteral("Custom order"));
    QCOMPARE(titles(), (QStringList{QStringLiteral("Charlie"), QStringLiteral("Alpha"), QStringLiteral("Bravo")}));
    QVERIFY(m_model->canReorder());

    m_model->setSort(static_cast<int>(m_model->sorts().indexOf(QStringLiteral("Title"))));
    QCOMPARE(titles(), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie")}));
    QVERIFY(!m_model->canReorder());
    // Longest first, as the library's collection queries order it.
    m_model->setSort(static_cast<int>(m_model->sorts().indexOf(QStringLiteral("Duration"))));
    QCOMPARE(titles(), (QStringList{QStringLiteral("Bravo"), QStringLiteral("Charlie"), QStringLiteral("Alpha")}));
    m_model->setSort(0);

    m_model->setFindText(QStringLiteral("rav"));
    QCOMPARE(titles(), QStringList{QStringLiteral("Bravo")});
    // The header still describes the whole playlist.
    QVERIFY(m_model->info().startsWith(QStringLiteral("3 songs")));
    QVERIFY(!m_model->canReorder());
    m_model->setFindText(QStringLiteral("zzz"));
    QCOMPARE(m_model->tracks()->count(), 0);
    QVERIFY(m_model->emptyText().contains(QStringLiteral("zzz")));
}

void CollectionViewModelTest::densityIsSaved()
{
    QVERIFY(!m_model->compact());
    QSignalSpy changed(m_model.get(), &CollectionViewModel::compactChanged);
    m_model->setCompact(true);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_player->settings.string(lmp::core::settings::keys::LibraryDetailDensity), QStringLiteral("Compact"));
    m_model->setCompact(true);
    QCOMPARE(changed.count(), 1);
}

void CollectionViewModelTest::selectionTogglesExtendsAndQueues()
{
    m_model->open(QStringLiteral("genre"), QStringLiteral("Rock"), QStringLiteral("Rock"));
    QCOMPARE(m_model->songCount(), 4);
    m_model->activate(0, true, false);
    QCOMPARE(m_model->selectedCount(), 1);
    QVERIFY(m_model->tracks()->data(m_model->tracks()->index(0), lmp::ui::TrackListModel::SelectedRole).toBool());
    m_model->activate(2, false, true);
    QCOMPARE(m_model->selectedCount(), 3);
    m_model->toggleSelected(1);
    QCOMPARE(m_model->selectedCount(), 2);

    // Queued songs reach the player; the selection clears.
    QVERIFY(m_actions->play(stored(QStringLiteral("Delta"))));
    settle();
    m_model->queueSelection();
    QCOMPARE(m_model->selectedCount(), 0);
    // The songs still to play (the current one is not among them).
    QCOMPARE(visibleTitles(*m_controller).size(), 2);

    // A plain click plays and clears any selection.
    m_model->activate(0, true, false);
    m_model->activate(1, false, false);
    settle();
    QCOMPARE(m_model->selectedCount(), 0);
    QCOMPARE(lastOpenedTitle(m_player->audio), m_model->tracks()->trackAt(1).title);
}

void CollectionViewModelTest::selectionMovesAndRemoves()
{
    const QString first = makePlaylist(QStringLiteral("First"),
                                       {QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie")});
    const QString second = makePlaylist(QStringLiteral("Second"), {QStringLiteral("Delta")});
    m_model->open(QStringLiteral("playlist"), first, QStringLiteral("First"));
    QCOMPARE(m_model->moveTargets().size(), 1);
    QCOMPARE(m_model->moveTargets().constFirst().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Second"));

    QSignalSpy notices(m_model.get(), &CollectionViewModel::notice);
    m_model->toggleSelected(0);
    m_model->toggleSelected(2);
    m_model->moveSelection(second);
    QCOMPARE(playlistTitles(first), QStringList{QStringLiteral("Bravo")});
    QCOMPARE(playlistTitles(second),
             (QStringList{QStringLiteral("Delta"), QStringLiteral("Alpha"), QStringLiteral("Charlie")}));
    QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.first().at(0).toString(), QStringLiteral("Moved 2 songs to Second"));
    QCOMPARE(titles(), QStringList{QStringLiteral("Bravo")});
    QCOMPARE(m_model->selectedCount(), 0);

    m_model->toggleSelected(0);
    m_model->removeSelection();
    QVERIFY(playlistTitles(first).isEmpty());
    QCOMPARE(m_model->songCount(), 0);
    QCOMPARE(notices.last().at(0).toString(), QStringLiteral("Removed 1 song"));

    // Not on a page that is not a user playlist.
    m_model->open(QStringLiteral("genre"), QStringLiteral("Rock"), QStringLiteral("Rock"));
    m_model->toggleSelected(0);
    m_model->removeSelection();
    QVERIFY(m_model->moveTargets().isEmpty());
    QCOMPARE(m_model->songCount(), 4);
}

void CollectionViewModelTest::dragStoresTheNewOrder()
{
    const QString key = makePlaylist(QStringLiteral("Mix"),
                                     {QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie")});
    m_model->open(QStringLiteral("playlist"), key, QStringLiteral("Mix"));
    QVERIFY(m_model->moveRow(0, 2));
    QCOMPARE(titles(), (QStringList{QStringLiteral("Bravo"), QStringLiteral("Charlie"), QStringLiteral("Alpha")}));
    QCOMPARE(playlistTitles(key), titles());
    QVERIFY(m_model->moveRow(2, 0));
    QCOMPARE(playlistTitles(key),
             (QStringList{QStringLiteral("Alpha"), QStringLiteral("Bravo"), QStringLiteral("Charlie")}));
    QVERIFY(!m_model->moveRow(0, 9));

    // Not while sorted another way.
    m_model->setSort(static_cast<int>(m_model->sorts().indexOf(QStringLiteral("Title"))));
    QVERIFY(!m_model->moveRow(0, 1));
}

void CollectionViewModelTest::routeOpensThePage()
{
    m_navigator->push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("album")},
                                                      {QStringLiteral("key"), QStringLiteral("Valleys")},
                                                      {QStringLiteral("title"), QStringLiteral("Valleys")}}));
    QCOMPARE(m_model->title(), QStringLiteral("Valleys"));
    QCOMPARE(m_model->songCount(), 1);

    m_navigator->push(Route(Route::Kind::Collection, {{QStringLiteral("kind"), QStringLiteral("artist")},
                                                      {QStringLiteral("key"), QStringLiteral("Canyon")},
                                                      {QStringLiteral("title"), QStringLiteral("Canyon")}}));
    // An artist route is the artist page's; the collection page keeps its album.
    QCOMPARE(m_model->title(), QStringLiteral("Valleys"));
    QCOMPARE(m_artist->name(), QStringLiteral("Canyon"));
}

void CollectionViewModelTest::artistGroupsAlbumsAndTopSongs()
{
    for (int play = 0; play < 3; ++play) {
        QVERIFY(m_player->tracks.recordPlayback(lmp::core::catalogSourceKey(stored(QStringLiteral("Charlie"))),
                                                static_cast<quint64>(play + 1)).isOk());
    }
    for (int extra = 0; extra < 3; ++extra) {
        addSong(QStringLiteral("Extra %1").arg(extra), QStringLiteral("Night Owls"), QStringLiteral("late hours"),
                QStringLiteral("Rock"));
    }
    m_artist->open(QStringLiteral("Night Owls"));
    // Most played first, five shown of seven.
    QCOMPARE(m_artist->topSongs()->trackAt(0).title, QStringLiteral("Charlie"));
    QCOMPARE(m_artist->topSongs()->count(), 5);
    QVERIFY(m_artist->hasMore());
    m_artist->setShowingAll(true);
    QCOMPARE(m_artist->topSongs()->count(), 7);

    // Albums grouped without regard to case; "Unknown Album" left out.
    QCOMPARE(m_artist->albums()->count(), 2);
    QCOMPARE(m_artist->info(), QStringLiteral("7 songs · 2 albums"));
    m_navigator->push(Route(Route::Kind::Library));
    m_artist->openAlbum(1);
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("album"));

    m_artist->playSong(0);
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("Charlie"));
}

QTEST_MAIN(CollectionViewModelTest)
#include "tst_collection_view_model.moc"
