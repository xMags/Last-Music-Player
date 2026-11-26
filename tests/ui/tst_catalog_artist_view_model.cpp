#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QFuture>
#include <QMap>
#include <QPromise>

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

// A release opened by its provider identity (named `id`), marked `kind`.
catalog::CatalogItem release(const QString& id, catalog::ReleaseKind kind = catalog::ReleaseKind::None)
{
    catalog::CatalogItem result = item(id, catalog::ResourceType::Album);
    result.releaseKind = kind;
    result.collectionTarget = lmp::core::account::LibraryArtistIdentity{id, {}, result.title, QStringLiteral("prov")};
    return result;
}

Track releaseSong(const QString& id, int n)
{
    Track track;
    track.remoteId = id + QStringLiteral("-song-%1").arg(n);
    track.title = QStringLiteral("Song %1 of ").arg(n) + id;
    track.artist = QStringLiteral("Mira Sol");
    track.sourceUrl = QStringLiteral("https://music.example.test/") + track.remoteId;
    return track;
}

// An artist with six top songs, an albums shelf (two albums, then the marked
// singles "s1" of one song, "s3" of three and "fail", whose load fails, the
// EP "e1" and the album "album1"), a singles shelf ("u1": one song, no mark;
// "slow": answers when the test says), a latest-release shelf (unmarked, as
// the catalogue sends it: "l1" of one song, "l10" of ten) and a
// similar-artists shelf.
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
        shelf(QStringLiteral("latest-release"), QStringLiteral("Latest Release"), catalog::ResourceType::Album,
              {release(QStringLiteral("l1")), release(QStringLiteral("l10"))}),
        shelf(QStringLiteral("albums"), QStringLiteral("Albums"), catalog::ResourceType::Album,
              {item(QStringLiteral("a1"), catalog::ResourceType::Album),
               item(QStringLiteral("a2"), catalog::ResourceType::Album),
               release(QStringLiteral("s1"), catalog::ReleaseKind::Single),
               release(QStringLiteral("s3"), catalog::ReleaseKind::Single),
               release(QStringLiteral("fail"), catalog::ReleaseKind::Single),
               release(QStringLiteral("e1"), catalog::ReleaseKind::Ep),
               release(QStringLiteral("album1"))}),
        shelf(QStringLiteral("singles"), QStringLiteral("Singles & EPs"), catalog::ResourceType::Album,
              {release(QStringLiteral("u1")), release(QStringLiteral("slow"))}),
        shelf(QStringLiteral("similar-artists"), QStringLiteral("Similar artists"), catalog::ResourceType::Artist,
              {item(QStringLiteral("r1"), catalog::ResourceType::Artist)}),
    };
    return detail;
}

using DetailResult = lmp::core::Result<catalog::Detail>;

// The release pages the provider answers, by identity id: "s1", "u1" and
// "slow" hold one song, "s3" three, "e1" two, "album1" ten, "l1" one and
// "l10" ten; "fail" fails.
// "slow" answers only when the test finishes `slow`.
struct ReleasePages {
    QFuture<DetailResult> answer(const lmp::core::account::LibraryArtistIdentity& identity)
    {
        ++calls[identity.catalogId];
        if (identity.catalogId == QLatin1StringView("slow")) {
            slow = std::make_shared<QPromise<DetailResult>>();
            slow->start();
            return slow->future();
        }
        if (identity.catalogId == QLatin1StringView("fail")) {
            return QtFuture::makeReadyValueFuture(
                DetailResult(lmp::core::Failure::of(lmp::core::FailureKind::Unknown, QStringLiteral("no"))));
        }
        const QMap<QString, int> songs{{QStringLiteral("s1"), 1}, {QStringLiteral("u1"), 1}, {QStringLiteral("s3"), 3},
                                       {QStringLiteral("e1"), 2}, {QStringLiteral("album1"), 10},
                                       {QStringLiteral("l1"), 1}, {QStringLiteral("l10"), 10}};
        return QtFuture::makeReadyValueFuture(DetailResult(page(identity.catalogId, songs.value(identity.catalogId))));
    }

    void answerSlow(int songs)
    {
        slow->addResult(DetailResult(page(QStringLiteral("slow"), songs)));
        slow->finish();
    }

    [[nodiscard]] static catalog::Detail page(const QString& id, int songs)
    {
        catalog::Detail detail;
        detail.item = release(id);
        for (int n = 1; n <= songs; ++n) {
            detail.tracks.append(releaseSong(id, n));
        }
        return detail;
    }

    QMap<QString, int> calls;
    std::shared_ptr<QPromise<DetailResult>> slow;
};

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
                  .follows = &follows,
                  .providerAlbum =
                      [this](const lmp::core::account::LibraryArtistIdentity& identity, const QString&) {
                          return releases.answer(identity);
                      }})
    {
    }

    // Opens the artist page (API key mode, from the provider) and waits for it.
    void openArtist()
    {
        modes.storeProvider();
        (void)modes.modes.start();
        navigator.push(Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("artist")},
                                                          {QStringLiteral("id"), QStringLiteral("123")},
                                                          {QStringLiteral("title"), QStringLiteral("Mira Sol")}}));
        QTRY_VERIFY(!artist.loading());
    }

    // The row of the release `id` on the shelf `key`.
    [[nodiscard]] int rowOf(const QString& key, const QString& id)
    {
        const QList<catalog::CatalogItem>& items = artist.sectionCards(key)->items();
        for (int row = 0; row < items.size(); ++row) {
            if (items.at(row).catalogId == id) {
                return row;
            }
        }
        return -1;
    }

    [[nodiscard]] QString playing() const
    {
        return controller->currentTrack() ? controller->currentTrack()->remoteId : QString();
    }

    ReleasePages releases;
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
    void aMarkedSingleOfOneSongPlaysIt();
    void aSingleOfSeveralSongsOpensItsPage();
    void anUnmarkedReleaseOnTheSinglesShelfOfOneSongPlaysIt();
    void anEpOrAnAlbumOpensItsPage();
    void aLatestReleaseOfOneSongPlaysIt();
    void aLatestReleaseOfSeveralSongsOpensItsPage();
    void aFailedLoadOpensThePage();
    void aLateAnswerAfterLeavingDoesNothing();
    void aDoubleClickLoadsOnce();
};

void CatalogArtistViewModelTest::aMarkedSingleOfOneSongPlaysIt()
{
    ArtistFixture fixture;
    fixture.openArtist();
    const qsizetype depth = fixture.navigator.depth();
    fixture.artist.openSectionCard(QStringLiteral("albums"), fixture.rowOf(QStringLiteral("albums"), QStringLiteral("s1")));
    QTRY_COMPARE(fixture.playing(), QStringLiteral("s1-song-1"));
    // In a context of just that song (nothing else queued), and no page opened.
    for (const lmp::core::QueueEntry& entry : fixture.controller->visibleQueue()) {
        QCOMPARE(entry.track.remoteId, QStringLiteral("s1-song-1"));
    }
    QCOMPARE(fixture.navigator.depth(), depth);
}

void CatalogArtistViewModelTest::aSingleOfSeveralSongsOpensItsPage()
{
    ArtistFixture fixture;
    fixture.openArtist();
    fixture.artist.openSectionCard(QStringLiteral("albums"), fixture.rowOf(QStringLiteral("albums"), QStringLiteral("s3")));
    QTRY_COMPARE(fixture.navigator.current().kind(), Route::Kind::SourceAlbum);
    QCOMPARE(fixture.navigator.current().param(QStringLiteral("catalogId")), QStringLiteral("s3"));
    QVERIFY(fixture.playing().isEmpty());
}

void CatalogArtistViewModelTest::anUnmarkedReleaseOnTheSinglesShelfOfOneSongPlaysIt()
{
    ArtistFixture fixture;
    fixture.openArtist();
    const qsizetype depth = fixture.navigator.depth();
    fixture.artist.openSectionCard(QStringLiteral("singles"), fixture.rowOf(QStringLiteral("singles"), QStringLiteral("u1")));
    QTRY_COMPARE(fixture.playing(), QStringLiteral("u1-song-1"));
    QCOMPARE(fixture.navigator.depth(), depth);
}

void CatalogArtistViewModelTest::anEpOrAnAlbumOpensItsPage()
{
    for (const QString& id : {QStringLiteral("e1"), QStringLiteral("album1")}) {
        ArtistFixture fixture;
        fixture.openArtist();
        fixture.artist.openSectionCard(QStringLiteral("albums"), fixture.rowOf(QStringLiteral("albums"), id));
        // At once, without loading the release first.
        QCOMPARE(fixture.navigator.current().kind(), Route::Kind::SourceAlbum);
        QCOMPARE(fixture.navigator.current().param(QStringLiteral("catalogId")), id);
        QCOMPARE(fixture.releases.calls.value(id), 0);
        QVERIFY(fixture.playing().isEmpty());
    }
}

void CatalogArtistViewModelTest::aLatestReleaseOfOneSongPlaysIt()
{
    ArtistFixture fixture;
    fixture.openArtist();
    const qsizetype depth = fixture.navigator.depth();
    fixture.artist.openSectionCard(QStringLiteral("latest-release"),
                                   fixture.rowOf(QStringLiteral("latest-release"), QStringLiteral("l1")));
    QTRY_COMPARE(fixture.playing(), QStringLiteral("l1-song-1"));
    QCOMPARE(fixture.navigator.depth(), depth);
}

void CatalogArtistViewModelTest::aLatestReleaseOfSeveralSongsOpensItsPage()
{
    ArtistFixture fixture;
    fixture.openArtist();
    fixture.artist.openSectionCard(QStringLiteral("latest-release"),
                                   fixture.rowOf(QStringLiteral("latest-release"), QStringLiteral("l10")));
    QTRY_COMPARE(fixture.navigator.current().kind(), Route::Kind::SourceAlbum);
    QCOMPARE(fixture.navigator.current().param(QStringLiteral("catalogId")), QStringLiteral("l10"));
    QVERIFY(fixture.playing().isEmpty());
}

void CatalogArtistViewModelTest::aFailedLoadOpensThePage()
{
    ArtistFixture fixture;
    fixture.openArtist();
    fixture.artist.openSectionCard(QStringLiteral("albums"), fixture.rowOf(QStringLiteral("albums"), QStringLiteral("fail")));
    QTRY_COMPARE(fixture.navigator.current().kind(), Route::Kind::SourceAlbum);
    QCOMPARE(fixture.navigator.current().param(QStringLiteral("catalogId")), QStringLiteral("fail"));
    QVERIFY(fixture.playing().isEmpty());
}

void CatalogArtistViewModelTest::aLateAnswerAfterLeavingDoesNothing()
{
    ArtistFixture fixture;
    fixture.openArtist();
    fixture.artist.openSectionCard(QStringLiteral("singles"), fixture.rowOf(QStringLiteral("singles"), QStringLiteral("slow")));
    QTRY_VERIFY(fixture.releases.slow != nullptr);
    fixture.navigator.push(Route(Route::Kind::Settings, {}));
    fixture.releases.answerSlow(1);
    QTest::qWait(50);
    QVERIFY(fixture.playing().isEmpty());
    QCOMPARE(fixture.navigator.current().kind(), Route::Kind::Settings);

    // Nor does a "more than one song" answer open the page.
    ArtistFixture again;
    again.openArtist();
    again.artist.openSectionCard(QStringLiteral("singles"), again.rowOf(QStringLiteral("singles"), QStringLiteral("slow")));
    QTRY_VERIFY(again.releases.slow != nullptr);
    again.navigator.push(Route(Route::Kind::Settings, {}));
    again.releases.answerSlow(4);
    QTest::qWait(50);
    QCOMPARE(again.navigator.current().kind(), Route::Kind::Settings);
}

void CatalogArtistViewModelTest::aDoubleClickLoadsOnce()
{
    ArtistFixture fixture;
    fixture.openArtist();
    const int row = fixture.rowOf(QStringLiteral("singles"), QStringLiteral("slow"));
    fixture.artist.openSectionCard(QStringLiteral("singles"), row);
    fixture.artist.openSectionCard(QStringLiteral("singles"), row);
    QTRY_VERIFY(fixture.releases.slow != nullptr);
    QSignalSpy started(fixture.controller.get(), &lmp::core::PlayerController::currentTrackChanged);
    fixture.releases.answerSlow(1);
    QTRY_COMPARE(fixture.playing(), QStringLiteral("slow-song-1"));
    QCOMPARE(fixture.releases.calls.value(QStringLiteral("slow")), 1);
    QCOMPARE(started.count(), 1);
}

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
    QCOMPARE(sections->count(), 4);
    QCOMPARE(sections->sections().at(0).key, QStringLiteral("latest-release"));
    QCOMPARE(sections->sections().at(1).key, QStringLiteral("albums"));
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
