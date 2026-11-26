#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QFuture>

#include <memory>
#include <vector>

#include "../core/CatalogFixtures.h"
#include "../core/FakePlaybackClock.h"
#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/db/CollectionRepository.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/db/GroupQueries.h"
#include "core/library/ArtistFollowService.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "ui/catalog/CatalogArtistViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/tracks/TrackMenuController.h"

namespace catalog = lmp::core::catalog;
using lmp::core::Track;
using lmp::ui::Route;

namespace {

catalog::CatalogItem item(const QString& id, catalog::ResourceType type)
{
    catalog::CatalogItem result;
    result.catalogId = id;
    result.title = QStringLiteral("Item ") + id;
    result.type = type;
    return result;
}

catalog::Shelf shelf(const QString& id, const QString& title, catalog::ResourceType type, QList<catalog::CatalogItem> items)
{
    catalog::Shelf result;
    result.id = id;
    result.title = title;
    result.type = type;
    result.items = std::move(items);
    return result;
}

// An artist with six top songs, an albums shelf and a similar-artists shelf.
catalog::Detail artistDetail()
{
    catalog::Detail detail;
    detail.item = item(QStringLiteral("123"), catalog::ResourceType::Artist);
    detail.item.title = QStringLiteral("Mira Sol");
    QList<catalog::CatalogItem> songs;
    for (int n = 1; n <= 6; ++n) {
        catalog::CatalogItem song = item(QString::number(n), catalog::ResourceType::Song);
        Track track;
        track.remoteId = QStringLiteral("song-%1").arg(n);
        track.title = song.title;
        track.artist = QStringLiteral("Mira Sol");
        track.sourceUrl = QStringLiteral("https://music.example.test/") + track.remoteId;
        song.track = track;
        songs.append(song);
    }
    detail.sections = {
        shelf(QStringLiteral("top-songs"), QStringLiteral("Top songs"), catalog::ResourceType::Song, songs),
        shelf(QStringLiteral("albums"), QStringLiteral("Albums"), catalog::ResourceType::Album,
              {item(QStringLiteral("a1"), catalog::ResourceType::Album),
               item(QStringLiteral("a2"), catalog::ResourceType::Album)}),
        shelf(QStringLiteral("similar-artists"), QStringLiteral("Similar artists"), catalog::ResourceType::Artist,
              {item(QStringLiteral("r1"), catalog::ResourceType::Artist)}),
    };
    return detail;
}

struct ArtistFixture {
    ArtistFixture()
        : catalogClient(modes.session.client)
        , catalogService({catalogClient, modes.session.service, modes.accounts, modes.session.settings, clock,
                          lmp::test::testTags()})
        , controller(player.makeController())
        , collections(player.library.database())
        , groups(player.library.database())
        , actions(lmp::core::TrackActions::Dependencies{*controller, player.tracks, collections, groups, modes.modes,
                                                         gate})
        , menu(actions)
        , followed(modes.library.database())
        , follows({modes.session.client, modes.session.service, modes.modes, followed, gate})
        , artist({.catalog = catalogService,
                  .client = catalogClient,
                  .accountClient = modes.session.client,
                  .session = modes.session.service,
                  .actions = actions,
                  .trackMenu = menu,
                  .navigator = navigator,
                  .share = nullptr,
                  .providerPages = [] { return true; },
                  .providerArtist =
                      [](const lmp::core::account::LibraryArtistIdentity&) {
                          return QtFuture::makeReadyValueFuture(lmp::core::Result<catalog::Detail>(artistDetail()));
                      },
                  .follows = &follows})
    {
    }

    lmp::test::ModeServiceFixture modes;
    FakePlaybackClock clock;
    catalog::CatalogClient catalogClient;
    catalog::CatalogService catalogService;
    PlayerFixture player;
    std::unique_ptr<lmp::core::PlayerController> controller;
    lmp::core::db::CollectionRepository collections;
    lmp::core::db::GroupQueries groups;
    lmp::core::UserDataOperationGate gate;
    lmp::core::TrackActions actions;
    lmp::ui::TrackMenuController menu;
    lmp::core::db::FollowedArtistRepository followed;
    lmp::core::ArtistFollowService follows;
    lmp::ui::Navigator navigator;
    lmp::ui::CatalogArtistViewModel artist;
};

// Counts every signal of `model` that makes a view build rows again.
struct RebuildSpy {
    explicit RebuildSpy(QAbstractItemModel* model)
        : reset(model, &QAbstractItemModel::modelReset)
        , inserted(model, &QAbstractItemModel::rowsInserted)
        , removed(model, &QAbstractItemModel::rowsRemoved)
    {
    }
    [[nodiscard]] qsizetype count() const { return reset.count() + inserted.count() + removed.count(); }
    QSignalSpy reset;
    QSignalSpy inserted;
    QSignalSpy removed;
};

} // namespace

// A catalogue artist page keeps its shelves and cards while the rest of its
// state changes.
class CatalogArtistViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void pageStateChangesRebuildNoShelf();
};

void CatalogArtistViewModelTest::pageStateChangesRebuildNoShelf()
{
    ArtistFixture fixture;
    fixture.modes.storeProvider();
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.modes.apiKeySelected());

    fixture.navigator.push(Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("artist")},
                                                              {QStringLiteral("id"), QStringLiteral("123")},
                                                              {QStringLiteral("title"), QStringLiteral("Mira Sol")}}));
    QTRY_VERIFY(!fixture.artist.loading());
    lmp::ui::ArtistSectionListModel* sections = fixture.artist.sections();
    QCOMPARE(sections->count(), 2);
    QCOMPARE(sections->sections().at(0).key, QStringLiteral("albums"));
    QCOMPARE(fixture.artist.sections(), sections);
    QCOMPARE(fixture.artist.sectionCards(QStringLiteral("albums")), fixture.artist.sectionCards(QStringLiteral("albums")));

    RebuildSpy shelves(sections);
    QSignalSpy shelfData(sections, &QAbstractItemModel::dataChanged);
    std::vector<std::unique_ptr<RebuildSpy>> cards;
    for (const lmp::ui::ArtistSectionListModel::Section& section : sections->sections()) {
        cards.push_back(std::make_unique<RebuildSpy>(fixture.artist.sectionCards(section.key)));
    }
    QSignalSpy changed(&fixture.artist, &lmp::ui::CatalogArtistViewModel::changed);

    // Show all, then Follow (busy, then its answer), then Show less.
    fixture.artist.setShowingAll(true);
    QVERIFY(fixture.artist.canFollow());
    fixture.artist.toggleFollow();
    QTRY_VERIFY(!fixture.artist.followBusy());
    QVERIFY(fixture.artist.followed());
    fixture.artist.setShowingAll(false);
    QVERIFY(changed.count() >= 4);

    QCOMPARE(shelves.count(), 0);
    QCOMPARE(shelfData.count(), 0);
    for (const std::unique_ptr<RebuildSpy>& spy : cards) {
        QCOMPARE(spy->count(), 0);
    }
}

QTEST_GUILESS_MAIN(CatalogArtistViewModelTest)
#include "tst_catalog_artist_view_model.moc"
