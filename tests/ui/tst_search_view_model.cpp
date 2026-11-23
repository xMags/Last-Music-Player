#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>
#include <utility>

#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/library/RecentSearchStore.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "core/model/CatalogSourceKey.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/search/SearchSource.h"
#include "ui/search/SearchViewModel.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::core::Track;
using lmp::ui::Navigator;
using lmp::ui::Route;
using lmp::ui::SearchHits;
using lmp::ui::SearchSource;
using lmp::ui::SearchViewModel;

namespace {

// Holds every search until the test answers it, in any order.
class SlowSource final : public SearchSource {
public:
    struct Pending {
        QString query;
        Done done;
    };

    void search(const QString& query, Done done) override { pending.append({query, std::move(done)}); }

    QList<Pending> pending;
};

// Answers from the real library, or holds the answer when slow.
class SwitchableSource final : public SearchSource {
public:
    SwitchableSource(SearchSource& local, SlowSource& slow)
        : m_local(local)
        , m_slow(slow)
    {
    }

    void search(const QString& query, Done done) override
    {
        if (slow) {
            m_slow.search(query, std::move(done));
        } else {
            m_local.search(query, std::move(done));
        }
    }

    bool slow = false;

private:
    SearchSource& m_local;
    SlowSource& m_slow;
};

} // namespace

// Browse and search: the landing, typing delay, grouped results, the top
// result, recent searches, and stale answers dropped.
class SearchViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void typingWaitsThenSearches();
    void resultsAreGrouped();
    void topResultPrefersAnExactName();
    void staleAnswersNeverReplaceNewerOnes();
    void clearingReturnsToTheLanding();
    void recentSearchesUseTheLegacySetting();
    void categoriesOpenGenresOrSearch();
    void cardsOpenTheirPages();

private:
    void addSong(const QString& title, const QString& artist, const QString& album, const QString& genre);
    void type(const QString& query);

    std::unique_ptr<PlayerFixture> m_player;
    std::unique_ptr<lmp::test::ModeServiceFixture> m_modes;
    std::unique_ptr<lmp::core::PlayerController> m_controller;
    std::unique_ptr<lmp::core::db::LibraryQueries> m_queries;
    std::unique_ptr<lmp::core::db::CollectionRepository> m_collections;
    std::unique_ptr<lmp::core::db::GroupQueries> m_groups;
    std::unique_ptr<lmp::core::UserDataOperationGate> m_gate;
    std::unique_ptr<lmp::core::TrackActions> m_actions;
    std::unique_ptr<lmp::ui::LocalSearchSource> m_local;
    std::unique_ptr<SlowSource> m_slow;
    std::unique_ptr<SwitchableSource> m_source;
    std::unique_ptr<lmp::ui::TrackMenuController> m_menu;
    std::unique_ptr<Navigator> m_navigator;
    std::unique_ptr<SearchViewModel> m_search;
};

void SearchViewModelTest::init()
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
    m_local = std::make_unique<lmp::ui::LocalSearchSource>(*m_queries, *m_groups);
    m_slow = std::make_unique<SlowSource>();
    m_source = std::make_unique<SwitchableSource>(*m_local, *m_slow);
    m_menu = std::make_unique<lmp::ui::TrackMenuController>(*m_actions);
    m_navigator = std::make_unique<Navigator>();
    m_search = std::make_unique<SearchViewModel>(SearchViewModel::Parts{
        *m_source, *m_queries, *m_groups, *m_actions, *m_controller, m_player->settings, *m_menu, *m_navigator});

    addSong(QStringLiteral("Night Drive"), QStringLiteral("Night Owls"), QStringLiteral("Late Hours"), QStringLiteral("Rock"));
    addSong(QStringLiteral("City Glow"), QStringLiteral("Night Owls"), QStringLiteral("City Lights"), QStringLiteral("Rock"));
    addSong(QStringLiteral("Desert Wind"), QStringLiteral("Canyon"), QStringLiteral("Valleys"), QStringLiteral("Ambient"));
}

void SearchViewModelTest::cleanup()
{
    m_search.reset();
    m_navigator.reset();
    m_menu.reset();
    m_source.reset();
    m_slow.reset();
    m_local.reset();
    m_actions.reset();
    m_gate.reset();
    m_groups.reset();
    m_collections.reset();
    m_queries.reset();
    m_controller.reset();
    m_modes.reset();
    m_player.reset();
}

void SearchViewModelTest::addSong(const QString& title, const QString& artist, const QString& album,
                                  const QString& genre)
{
    Track track = m_player->song(title);
    track.artist = artist;
    track.album = album;
    track.genre = genre;
    QVERIFY(m_player->tracks.upsertLocalTrack(track, lmp::core::catalogSourceKey(track)).isOk());
}

void SearchViewModelTest::type(const QString& query)
{
    m_search->setQuery(query);
    QTRY_VERIFY_WITH_TIMEOUT(m_search->state() != QLatin1StringView("landing")
                                 && m_search->state() != QLatin1StringView("searching"),
                             2000);
}

void SearchViewModelTest::typingWaitsThenSearches()
{
    QCOMPARE(m_search->state(), QStringLiteral("landing"));
    m_search->setQuery(QStringLiteral("night"));
    // Nothing until typing pauses.
    QCOMPARE(m_search->generation(), 0u);
    QTRY_COMPARE_WITH_TIMEOUT(m_search->state(), QStringLiteral("results"), 2000);
    QCOMPARE(m_search->generation(), 1u);
    QCOMPARE(m_search->songs()->count(), 2);

    // Enter searches at once.
    m_search->setQuery(QStringLiteral("zzz"));
    m_search->searchNow();
    QCOMPARE(m_search->state(), QStringLiteral("empty"));
}

void SearchViewModelTest::resultsAreGrouped()
{
    QVERIFY(m_actions->addToNewPlaylist(m_player->song(QStringLiteral("Night Drive")), QStringLiteral("Night mix")));
    type(QStringLiteral("night"));
    QCOMPARE(m_search->songs()->count(), 2);
    QCOMPARE(m_search->topSongs()->count(), 2);
    // Artists and albums from the song hits that match by name.
    QCOMPARE(m_search->artists()->count(), 1);
    QCOMPARE(m_search->artists()->cardAt(0).title, QStringLiteral("Night Owls"));
    QCOMPARE(m_search->artists()->cardAt(0).kind, QStringLiteral("artist"));
    QCOMPARE(m_search->playlists()->count(), 1);
    QCOMPARE(m_search->playlists()->cardAt(0).title, QStringLiteral("Night mix"));

    // All songs sort without a new search.
    const quint64 generation = m_search->generation();
    m_search->setSongSort(1);
    QCOMPARE(m_search->songs()->trackAt(0).title, QStringLiteral("City Glow"));
    QCOMPARE(m_search->generation(), generation);
}

void SearchViewModelTest::topResultPrefersAnExactName()
{
    type(QStringLiteral("canyon"));
    QCOMPARE(m_search->topKind(), QStringLiteral("artist"));
    QCOMPARE(m_search->topTitle(), QStringLiteral("Canyon"));
    m_search->openTop();
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("artist"));

    m_search->setQuery(QStringLiteral("desert"));
    m_search->searchNow();
    QCOMPARE(m_search->topKind(), QStringLiteral("song"));
    QCOMPARE(m_search->topTitle(), QStringLiteral("Desert Wind"));
    m_search->openTop();
    settle();
    QCOMPARE(lastOpenedTitle(m_player->audio), QStringLiteral("Desert Wind"));
}

void SearchViewModelTest::staleAnswersNeverReplaceNewerOnes()
{
    m_source->slow = true;
    m_search->setQuery(QStringLiteral("night"));
    m_search->searchNow();
    QCOMPARE(m_search->state(), QStringLiteral("searching"));
    m_search->setQuery(QStringLiteral("desert"));
    m_search->searchNow();
    QCOMPARE(m_slow->pending.size(), 2);

    // The newer answer arrives first, then the older one: it is dropped.
    SearchHits desert;
    desert.songs.append(m_player->song(QStringLiteral("Desert Wind")));
    m_slow->pending.at(1).done(desert);
    QCOMPARE(m_search->state(), QStringLiteral("results"));
    QCOMPARE(m_search->songs()->trackAt(0).title, QStringLiteral("Desert Wind"));
    SearchHits night;
    night.songs.append(m_player->song(QStringLiteral("Night Drive")));
    m_slow->pending.at(0).done(night);
    QCOMPARE(m_search->songs()->count(), 1);
    QCOMPARE(m_search->songs()->trackAt(0).title, QStringLiteral("Desert Wind"));

    // A search still on its way when the box is cleared is dropped too.
    m_search->setQuery(QStringLiteral("city"));
    m_search->searchNow();
    m_search->setQuery(QString());
    m_slow->pending.at(2).done(night);
    QCOMPARE(m_search->state(), QStringLiteral("landing"));
}

void SearchViewModelTest::clearingReturnsToTheLanding()
{
    type(QStringLiteral("night"));
    m_search->setQuery(QStringLiteral("   "));
    QCOMPARE(m_search->state(), QStringLiteral("landing"));
}

void SearchViewModelTest::recentSearchesUseTheLegacySetting()
{
    QSignalSpy recent(m_search.get(), &SearchViewModel::recentSearchesChanged);
    m_search->setQuery(QStringLiteral("night"));
    m_search->searchNow();
    QCOMPARE(m_search->recentSearches(), QStringList{QStringLiteral("night")});
    // A query typed and left on screen joins after the dwell.
    m_search->setQuery(QStringLiteral("desert"));
    QTRY_COMPARE_WITH_TIMEOUT(m_search->recentSearches().size(), 2, 4000);
    QCOMPARE(m_search->recentSearches().constFirst(), QStringLiteral("desert"));
    // Stored in the legacy form.
    QCOMPARE(lmp::core::RecentSearchStore::load(m_player->settings), m_search->recentSearches());

    m_search->removeRecent(0);
    QCOMPARE(m_search->recentSearches(), QStringList{QStringLiteral("night")});
    m_search->useRecent(0);
    QCOMPARE(m_search->query(), QStringLiteral("night"));
    QCOMPARE(m_search->state(), QStringLiteral("results"));
    m_search->clearRecents();
    QVERIFY(m_search->recentSearches().isEmpty());
    QVERIFY(lmp::core::RecentSearchStore::load(m_player->settings).isEmpty());
    QVERIFY(recent.count() >= 4);
}

void SearchViewModelTest::categoriesOpenGenresOrSearch()
{
    m_search->refreshLanding();
    lmp::ui::CollectionCardModel* tiles = m_search->categories();
    QCOMPARE(tiles->count(), SearchViewModel::CategoryTileCount);
    // The library's genres lead and open their pages; shortcuts search.
    QCOMPARE(tiles->cardAt(0).kind, QStringLiteral("genre"));
    m_search->openCategory(0);
    QCOMPARE(m_navigator->current().param(QStringLiteral("kind")), QStringLiteral("genre"));
    int shortcut = -1;
    for (int row = 0; row < tiles->count(); ++row) {
        if (tiles->cardAt(row).kind == QLatin1StringView("shortcut")) {
            shortcut = row;
            break;
        }
    }
    QVERIFY(shortcut >= 0);
    m_search->openCategory(shortcut);
    QCOMPARE(m_search->query(), tiles->cardAt(shortcut).title);
}

void SearchViewModelTest::cardsOpenTheirPages()
{
    type(QStringLiteral("valleys"));
    QCOMPARE(m_search->albums()->count(), 1);
    m_search->openCard(QStringLiteral("albums"), 0);
    QCOMPARE(m_navigator->current().kind(), Route::Kind::Collection);
    QCOMPARE(m_navigator->current().param(QStringLiteral("title")), QStringLiteral("Valleys"));
    // Opening a result counts as a search worth remembering.
    QCOMPARE(m_search->recentSearches().constFirst(), QStringLiteral("valleys"));
}

QTEST_MAIN(SearchViewModelTest)
#include "tst_search_view_model.moc"
