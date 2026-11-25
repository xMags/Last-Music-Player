#include <QCoreApplication>
#include <QObject>
#include <QString>
#include <QTest>

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
        , page({.catalog = catalog, .editor = editor, .actions = actions, .trackMenu = menu, .navigator = navigator})
    {
    }

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
};

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
