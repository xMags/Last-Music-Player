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
#include "core/library/CollectionActions.h"
#include "core/library/HomeMixService.h"
#include "core/library/LibraryScanner.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "ui/home/HomeCatalogFeed.h"
#include "ui/home/HomeViewModel.h"
#include "ui/library/CollectionViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::Track;
using lmp::ui::HomeViewModel;
using lmp::ui::Navigator;
using lmp::ui::Route;

// Home in its offline form: greeting, hero, shortcuts, sections and their
// actions, and today's mixes (made again only when the day changes).
class HomeViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void greetingFollowsTheHour();
    void catalogRetriesBackOffToFiveMinutes();
    void emptyLibraryHasNoSections();
    void sectionsFollowTheLibrary();
    void heroIsTheLastPlay();
    void shortcutsAreWholeRows();
    void playlistPopupOffersTheNewestEight();
    void mixesAreRemadeOnlyOnANewDay();
    void cardsPlayAndOpen();

private:
    void addSong(const QString& title, const QString& artist, const QString& genre);
    void play(const QString& title, quint64 order);
    [[nodiscard]] QStringList sectionKeys() const;
    [[nodiscard]] Track stored(const QString& title);

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
    std::unique_ptr<lmp::core::HomeMixService> m_mixes;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<Navigator> m_navigator;
    std::unique_ptr<lmp::ui::CollectionViewModel> m_collection;
    std::unique_ptr<HomeViewModel> m_home;
    QDate m_today{2026, 11, 23};
    double m_added = 0;
    QDateTime m_now{QDate(2026, 11, 23), QTime(9, 0)};
};

void HomeViewModelTest::init()
{
    m_added = 0;
    m_today = QDate(2026, 11, 23);
    m_now = QDateTime(QDate(2026, 11, 23), QTime(9, 0));
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
    m_mixes = std::make_unique<lmp::core::HomeMixService>(*m_queries, *m_groups, [this] { return m_today; });
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_navigator = std::make_unique<Navigator>();
    m_collection = std::make_unique<lmp::ui::CollectionViewModel>(lmp::ui::CollectionViewModel::Parts{
        *m_queries, *m_actions, *m_collectionActions, *m_mixes, *m_controller, m_player->settings, *m_menu, *m_navigator});
    m_home = std::make_unique<HomeViewModel>(HomeViewModel::Parts{.library = *m_queries,
                                                                  .actions = *m_actions,
                                                                  .collections = *m_collectionActions,
                                                                  .player = *m_controller,
                                                                  .scanner = *m_scanner,
                                                                  .mixes = *m_mixes,
                                                                  .trackMenu = *m_menu,
                                                                  .navigator = *m_navigator,
                                                                  .now = [this] { return m_now; }});
}

void HomeViewModelTest::cleanup()
{
    m_home.reset();
    m_collection.reset();
    m_navigator.reset();
    m_menu.reset();
    m_mixes.reset();
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

void HomeViewModelTest::addSong(const QString& title, const QString& artist, const QString& genre)
{
    Track track = m_player->song(title);
    track.artist = artist;
    track.genre = genre;
    // Recently Added lists only songs with an added time.
    track.dateAddedSortKey = ++m_added;
    QVERIFY(m_player->tracks.upsertLocalTrack(track, lmp::core::catalogSourceKey(track)).isOk());
}

void HomeViewModelTest::play(const QString& title, quint64 order)
{
    QVERIFY(m_player->tracks.recordPlayback(lmp::core::catalogSourceKey(m_player->song(title)), order).isOk());
}

QStringList HomeViewModelTest::sectionKeys() const
{
    QStringList keys;
    lmp::ui::HomeSectionListModel* sections = m_home->sections();
    for (int row = 0; row < sections->count(); ++row) {
        keys.append(sections->index(row).data(lmp::ui::HomeSectionListModel::KeyRole).toString());
    }
    return keys;
}

Track HomeViewModelTest::stored(const QString& title)
{
    lmp::core::db::TrackQuery query;
    query.searchText = title;
    const QList<Track> found = m_queries->loadTracksForQuery(query);
    return found.isEmpty() ? Track{} : found.constFirst();
}

void HomeViewModelTest::greetingFollowsTheHour()
{
    QCOMPARE(HomeViewModel::greetingTimeFor(3), QStringLiteral("moon"));
    QCOMPARE(HomeViewModel::greetingTimeFor(4), QStringLiteral("sunrise"));
    QCOMPARE(HomeViewModel::greetingTimeFor(12), QStringLiteral("sun"));
    QCOMPARE(HomeViewModel::greetingTimeFor(17), QStringLiteral("sunset"));
    QCOMPARE(HomeViewModel::greetingTimeFor(22), QStringLiteral("moon"));
    QCOMPARE(m_home->salutation(), QStringLiteral("Good morning"));
    QSignalSpy changed(m_home.get(), &HomeViewModel::greetingChanged);
    m_now.setTime(QTime(18, 30));
    m_home->refreshGreeting();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(m_home->salutation(), QStringLiteral("Good evening"));
    m_home->refreshGreeting();
    QCOMPARE(changed.count(), 1);
}

void HomeViewModelTest::emptyLibraryHasNoSections()
{
    m_home->refresh();
    QVERIFY(m_home->empty());
    QVERIFY(sectionKeys().isEmpty());
    QVERIFY(!m_home->hasHero());
}

void HomeViewModelTest::sectionsFollowTheLibrary()
{
    addSong(QStringLiteral("One"), QStringLiteral("Night Owls"), QStringLiteral("Rock"));
    addSong(QStringLiteral("Two"), QStringLiteral("Canyon"), QStringLiteral("Ambient"));
    m_home->refresh();
    QVERIFY(!m_home->empty());
    // Nothing played or liked yet: the mixes and Recently Added. Recently
    // Added's system playlist is the one shortcut, so the hero shows.
    QCOMPARE(sectionKeys(), (QStringList{QStringLiteral("hero"), QStringLiteral("mixes"), QStringLiteral("recentlyAdded")}));
    lmp::ui::HomeSectionListModel* sections = m_home->sections();
    QCOMPARE(sections->index(1).data(lmp::ui::HomeSectionListModel::TierRole).toString(), QStringLiteral("For you"));
    QCOMPARE(sections->index(2).data(lmp::ui::HomeSectionListModel::TierRole).toString(),
             QStringLiteral("From your library"));
    QCOMPARE(sections->index(1).data(lmp::ui::HomeSectionListModel::CapabilityRole).toString(), QStringLiteral("local"));
    QCOMPARE(m_home->mixes()->count(), 9);

    // Three songs played: Most Played shows; a like brings Liked Songs.
    addSong(QStringLiteral("Three"), QStringLiteral("Canyon"), QStringLiteral("Ambient"));
    play(QStringLiteral("One"), 1);
    play(QStringLiteral("Two"), 2);
    play(QStringLiteral("Three"), 3);
    QVERIFY(m_actions->toggleLike(stored(QStringLiteral("One"))).value_or(false));
    m_home->refresh();
    QCOMPARE(sectionKeys(), (QStringList{QStringLiteral("hero"), QStringLiteral("mixes"), QStringLiteral("listenAgain"),
                                         QStringLiteral("mostPlayed"), QStringLiteral("liked"),
                                         QStringLiteral("recentlyAdded")}));
    // Only the first section of a tier carries its label.
    QVERIFY(sections->index(2).data(lmp::ui::HomeSectionListModel::TierRole).toString().isEmpty());
    QCOMPARE(sections->index(3).data(lmp::ui::HomeSectionListModel::TierRole).toString(),
             QStringLiteral("From your library"));
}

void HomeViewModelTest::heroIsTheLastPlay()
{
    addSong(QStringLiteral("Old"), QStringLiteral("A"), {});
    addSong(QStringLiteral("New"), QStringLiteral("B"), {});
    play(QStringLiteral("Old"), 1);
    play(QStringLiteral("New"), 2);
    m_home->refresh();
    QVERIFY(m_home->hasHero());
    QCOMPARE(m_home->heroTitle(), QStringLiteral("New"));
    QCOMPARE(m_home->heroArtist(), QStringLiteral("B"));
    // Listen Again holds the rest, not the hero again.
    QCOMPARE(m_home->listenAgain()->count(), 1);
    QCOMPARE(m_home->listenAgain()->trackAt(0).title, QStringLiteral("Old"));
    m_home->playHero();
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("New"));
}

void HomeViewModelTest::shortcutsAreWholeRows()
{
    for (const QString& title : {QStringLiteral("One"), QStringLiteral("Two")}) {
        addSong(title, QStringLiteral("A"), {});
    }
    // Recently Added plus four playlists with songs, one without: five, cut to three.
    for (int index = 0; index < 4; ++index) {
        QVERIFY(m_actions->addToNewPlaylist(stored(QStringLiteral("One")), QStringLiteral("P%1").arg(index)));
    }
    QVERIFY(m_collectionActions->createPlaylist(QStringLiteral("Empty")));
    m_home->refresh();
    QCOMPARE(m_home->quickTiles()->count(), 3);
    QCOMPARE(m_home->quickTiles()->cardAt(0).key, QStringLiteral("smart-recent"));
    QCOMPARE(m_home->quickTiles()->cardAt(1).kind, QStringLiteral("playlist"));
    m_home->playQuickTile(1);
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("One"));
}

void HomeViewModelTest::playlistPopupOffersTheNewestEight()
{
    addSong(QStringLiteral("One"), QStringLiteral("A"), {});
    addSong(QStringLiteral("Two"), QStringLiteral("A"), {});
    for (int index = 0; index < 9; ++index) {
        QVERIFY(m_actions->addToNewPlaylist(stored(QStringLiteral("One")), QStringLiteral("P%1").arg(index)));
    }
    m_menu->prepare(stored(QStringLiteral("Two")), {});
    m_menu->trigger(QStringLiteral("playlist"));
    const QVariantList choices = m_menu->playlistChoices();
    QCOMPARE(choices.size(), 8);
    QCOMPARE(choices.first().toMap().value(QStringLiteral("count")).toInt(), 1);
    // Only an offered playlist takes the song.
    const QString key = choices.first().toMap().value(QStringLiteral("key")).toString();
    m_menu->addToPlaylist(QStringLiteral("playlist:unknown"));
    m_menu->addToPlaylist(key);
    m_menu->trigger(QStringLiteral("playlist"));
    QCOMPARE(m_menu->playlistChoices().first().toMap().value(QStringLiteral("count")).toInt(), 2);
    // A blank name makes nothing.
    m_menu->createPlaylist(QStringLiteral("   "));
    QCOMPARE(m_actions->playlists().size(), 9);
}

void HomeViewModelTest::mixesAreRemadeOnlyOnANewDay()
{
    addSong(QStringLiteral("One"), QStringLiteral("A"), QStringLiteral("Rock"));
    QSignalSpy remade(m_mixes.get(), &lmp::core::HomeMixService::mixesChanged);
    m_home->refresh();
    QCOMPARE(remade.count(), 1);
    // Plays later the same day leave the mixes alone.
    addSong(QStringLiteral("Two"), QStringLiteral("B"), QStringLiteral("Rock"));
    m_home->refresh();
    QCOMPARE(remade.count(), 1);
    QCOMPARE(m_mixes->mix(QStringLiteral("fresh"))->tracks.size(), 1);
    // The next day they are made again from the library.
    m_today = m_today.addDays(1);
    m_home->refresh();
    QCOMPARE(remade.count(), 2);
    QCOMPARE(m_mixes->mix(QStringLiteral("fresh"))->tracks.size(), 2);
}

void HomeViewModelTest::cardsPlayAndOpen()
{
    addSong(QStringLiteral("One"), QStringLiteral("A"), {});
    addSong(QStringLiteral("Two"), QStringLiteral("B"), {});
    m_home->refresh();
    m_home->playMix(0);
    settle();
    QVERIFY(!lastOpenedTitle(m_player->audio).isEmpty());

    // The mix menu: Play now, Play next, Add to queue, Shuffle (Download and
    // Share join where they exist); a click on the card opens the mix.
    m_home->prepareMixMenu(0);
    QCOMPARE(m_home->cardMenu()->idAt(0), QStringLiteral("play"));
    QCOMPARE(m_home->cardMenu()->idAt(3), QStringLiteral("shuffle"));
    QCOMPARE(m_home->cardMenu()->count(), 4);
    m_home->triggerCardMenu(QStringLiteral("open"));
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("mix"));
    // The mix's page shows its songs and name.
    QCOMPARE(m_collection->title(), QStringLiteral("Daily Mix 1"));
    QCOMPARE(m_collection->eyebrow(), QStringLiteral("AUTO PLAYLIST"));
    QCOMPARE(m_collection->songCount(), 2);
    m_collection->setFindText(QStringLiteral("Two"));
    QCOMPARE(m_collection->tracks()->count(), 1);

    m_home->seeAll(QStringLiteral("recentlyAdded"));
    QCOMPARE(m_navigator->current().param(QStringLiteral("key")), QStringLiteral("smart-recent"));
    m_home->seeAll(QStringLiteral("mixes"));
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Library);
    // openAutoMixes: the playlists tab on Auto mixes.
    QCOMPARE(m_navigator->current().param(QStringLiteral("filter")), QStringLiteral("auto"));
}

void HomeViewModelTest::catalogRetriesBackOffToFiveMinutes()
{
    using std::chrono::milliseconds;
    QCOMPARE(lmp::ui::nextCatalogRetryDelay(milliseconds(0)), milliseconds(30'000));
    QCOMPARE(lmp::ui::nextCatalogRetryDelay(milliseconds(30'000)), milliseconds(60'000));
    QCOMPARE(lmp::ui::nextCatalogRetryDelay(milliseconds(240'000)), milliseconds(300'000));
    QCOMPARE(lmp::ui::nextCatalogRetryDelay(milliseconds(300'000)), milliseconds(300'000));
}

QTEST_MAIN(HomeViewModelTest)
#include "tst_home_view_model.moc"
