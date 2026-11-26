#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrl>

#include "../core/CatalogFixtures.h"
#include "../core/FakePlaybackClock.h"
#include "../core/ModeServiceFixture.h"
#include "../core/PlayerFixture.h"
#include "core/account/AccountLibraryEditor.h"
#include "core/account/MusicSyncService.h"
#include "core/catalog/CatalogClient.h"
#include "core/catalog/CatalogService.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/library/TrackActions.h"
#include "core/library/UserDataOperationGate.h"
#include "ui/catalog/CatalogPageViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"
#include "ui/tracks/TrackMenuController.h"

using lmp::test::FakeHttpTransport;
using lmp::ui::Navigator;
using lmp::ui::Route;

namespace {

const QString kSongAlbumPath = QStringLiteral("/v1/music/catalog/songs/album");
const QString kSinglesPath = QStringLiteral("/v1/music/catalog/artists/detail/view/singles");

// A provider release page of `songs` songs.
lmp::core::catalog::Detail releasePage(const QString& id, int songs)
{
    lmp::core::catalog::Detail detail;
    detail.item.catalogId = id;
    detail.item.type = lmp::core::catalog::ResourceType::Album;
    for (int n = 1; n <= songs; ++n) {
        lmp::core::Track track;
        track.remoteId = id + QStringLiteral("-song-%1").arg(n);
        track.title = QStringLiteral("Song %1").arg(n);
        track.sourceUrl = QStringLiteral("https://music.example.test/") + track.remoteId;
        detail.tracks.append(track);
    }
    return detail;
}

struct PageFixture {
    PageFixture()
        : outbox(modes.library.database())
        , sync({.transport = modes.session.client,
                .session = modes.session.service,
                .mode = modes.modes,
                .accounts = modes.accounts,
                .outbox = outbox,
                .settings = modes.session.settings,
                .gate = modes.session.gate,
                .clock = clock,
                .nowUtc = [] { return QStringLiteral("2026-11-25T10:00:00Z"); }})
        , editor({.client = modes.session.client,
                  .session = modes.session.service,
                  .mode = modes.modes,
                  .accounts = modes.accounts,
                  .outbox = outbox,
                  .sync = sync,
                  .gate = modes.session.gate,
                  .nowUtc = [] { return QStringLiteral("2026-11-25T10:00:00Z"); },
                  .newEventId = [] { return QStringLiteral("{EVENT}"); }})
        , catalogClient(modes.session.client)
        , catalog({catalogClient, modes.session.service, modes.accounts, modes.session.settings, clock,
                   lmp::test::testTags()})
        , controller(player.makeController())
        , collections(player.library.database())
        , groups(player.library.database())
        , actions(lmp::core::TrackActions::Dependencies{*controller, player.tracks, collections, groups, modes.modes,
                                                         gate})
        , menu(actions)
        , page({.catalog = catalog,
                .editor = editor,
                .actions = actions,
                .trackMenu = menu,
                .navigator = navigator,
                .providerPages = [] { return true; },
                .providerAlbum =
                    [this](const lmp::core::account::LibraryArtistIdentity& identity, const QString&) {
                        ++releaseLoads;
                        return QtFuture::makeReadyValueFuture(
                            lmp::core::Result<lmp::core::catalog::Detail>(releasePage(identity.catalogId, 1)));
                    }})
    {
    }

    int releaseLoads = 0;

    lmp::test::ModeServiceFixture modes;
    lmp::core::db::AccountOutboxRepository outbox;
    FakePlaybackClock clock;
    lmp::core::account::MusicSyncService sync;
    lmp::core::account::AccountLibraryEditor editor;
    lmp::core::catalog::CatalogClient catalogClient;
    lmp::core::catalog::CatalogService catalog;
    PlayerFixture player;
    std::unique_ptr<lmp::core::PlayerController> controller;
    lmp::core::db::CollectionRepository collections;
    lmp::core::db::GroupQueries groups;
    lmp::core::UserDataOperationGate gate;
    lmp::core::TrackActions actions;
    lmp::ui::TrackMenuController menu;
    Navigator navigator;
    lmp::ui::CatalogPageViewModel page;
};

} // namespace

// The catalogue page's requests and the page the user is on.
class CatalogPageViewModelTest : public QObject {
    Q_OBJECT

private slots:
    void aLateAlbumLookupLeavesTheNewPageAlone();
    void aSingleOnTheSinglesPagePlaysItsSong();
    void anotherAlbumShowsNoCoverWhileItLoads();
};

void CatalogPageViewModelTest::anotherAlbumShowsNoCoverWhileItLoads()
{
    PageFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    settle();
    const auto albumAnswer = [](const QString& id) {
        return FakeHttpTransport::ok((QStringLiteral(R"({"album":{"resourceType":"album","catalogId":")") + id
                                      + QStringLiteral(R"(","title":"Album )") + id
                                      + QStringLiteral(R"(","artworkUrl":"https://cdn.example.test/artwork/)") + id
                                      + QStringLiteral(R"(.jpg"},"tracks":[]})"))
                                         .toUtf8());
    };
    const auto openAlbum = [&fixture](const QString& id) {
        fixture.navigator.push(Route(Route::Kind::CatalogDetail, {{QStringLiteral("type"), QStringLiteral("album")},
                                                                  {QStringLiteral("id"), id},
                                                                  {QStringLiteral("title"), QStringLiteral("Album ") + id}}));
    };
    const QString firstPath = QStringLiteral("/v1/music/catalog/albums/111");
    const QString secondPath = QStringLiteral("/v1/music/catalog/albums/222");

    openAlbum(QStringLiteral("111"));
    settle();
    QVERIFY(fixture.modes.session.transport.answer(firstPath, albumAnswer(QStringLiteral("111"))));
    QTRY_VERIFY(!fixture.page.loading());
    const QUrl firstCover = fixture.page.artworkUrl();
    QVERIFY(!firstCover.isEmpty());

    // The next album: no cover (the last one's least of all) until it answers.
    openAlbum(QStringLiteral("222"));
    settle();
    QVERIFY(fixture.page.loading());
    QVERIFY(fixture.page.artworkUrl().isEmpty());
    QVERIFY(fixture.modes.session.transport.answer(secondPath, albumAnswer(QStringLiteral("222"))));
    QTRY_VERIFY(!fixture.page.loading());
    QVERIFY(!fixture.page.artworkUrl().isEmpty());
    QVERIFY(fixture.page.artworkUrl() != firstCover);
}

void CatalogPageViewModelTest::aSingleOnTheSinglesPagePlaysItsSong()
{
    PageFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    settle();
    fixture.modes.session.transport.script(
        kSinglesPath, FakeHttpTransport::ok(R"({"items":[{"catalogId":"one","name":"One","provider":"prov",)"
                                            R"("releaseType":"single","sourceUrl":"https://music.example.test/one"}]})"));

    // An artist's Singles & EPs in full, from the provider.
    fixture.navigator.push(Route(Route::Kind::ArtistSection, {{QStringLiteral("section"), QStringLiteral("singles")},
                                                              {QStringLiteral("title"), QStringLiteral("Singles & EPs")},
                                                              {QStringLiteral("artistName"), QStringLiteral("Mira Sol")},
                                                              {QStringLiteral("provider"), QStringLiteral("prov")},
                                                              {QStringLiteral("name"), QStringLiteral("Mira Sol")}}));
    QTRY_VERIFY(!fixture.page.loading());
    QCOMPARE(fixture.page.cards()->items().size(), 1);
    const qsizetype depth = fixture.navigator.depth();

    // A click plays the single's one song; no page opens.
    fixture.page.openCard(0);
    QTRY_VERIFY(fixture.controller->currentTrack().has_value());
    QCOMPARE(fixture.controller->currentTrack()->remoteId, QStringLiteral("one-song-1"));
    QCOMPARE(fixture.navigator.depth(), depth);

    // Its menu is the song's menu.
    QSignalSpy looked(&fixture.page, &lmp::ui::CatalogPageViewModel::cardSongLooked);
    QVERIFY(fixture.page.lookUpCardSong(0));
    QTRY_COMPARE(looked.count(), 1);
    QCOMPARE(looked.first().at(1).toBool(), true);
}

void CatalogPageViewModelTest::aLateAlbumLookupLeavesTheNewPageAlone()
{
    PageFixture fixture;
    (void)fixture.modes.modes.start();
    QVERIFY(fixture.modes.validate(QStringLiteral("owner-a")));
    QVERIFY(fixture.modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    settle();

    // A song's "Go to album": the page looks the album up.
    fixture.navigator.push(Route(Route::Kind::SourceAlbum, {{QStringLiteral("finding"), QStringLiteral("1")},
                                                            {QStringLiteral("title"), QStringLiteral("Song")},
                                                            {QStringLiteral("artist"), QStringLiteral("Mira Sol")},
                                                            {QStringLiteral("album"), QStringLiteral("Album")}}));
    settle();
    QCOMPARE(fixture.modes.session.transport.requestCount(kSongAlbumPath), 1);

    // The user goes Home before it answers.
    fixture.navigator.push(Route(Route::Kind::Home));
    QVERIFY(fixture.modes.session.transport.answer(
        kSongAlbumPath, FakeHttpTransport::ok(R"({"album":{"provider":"tag-a","catalogId":"123"}})")));
    settle();
    QCOMPARE(fixture.navigator.current().kind(), Route::Kind::Home);
}

QTEST_GUILESS_MAIN(CatalogPageViewModelTest)
#include "tst_catalog_page_view_model.moc"
