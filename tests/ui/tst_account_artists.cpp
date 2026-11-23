#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QUrlQuery>

#include "../core/ModeServiceFixture.h"
#include "core/account/LibraryArtistContract.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/library/ArtistFollowService.h"
#include "core/library/UserDataOperationGate.h"
#include "ui/library/AccountArtistsViewModel.h"
#include "ui/navigation/Navigator.h"
#include "ui/navigation/Route.h"

using lmp::core::FailureKind;
using lmp::core::Result;
using lmp::test::FakeHttpTransport;
namespace account = lmp::core::account;

namespace {

const QString kArtistsPath = QStringLiteral("/v1/music/library/artists");

void settle()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
    }
}

QByteArray artistJson(const QString& id, const QString& name, bool followed = false)
{
    return "{\"id\":\"" + id.toUtf8() + "\",\"name\":\"" + name.toUtf8()
        + "\",\"provider\":\"source-a\",\"trackCount\":3,\"albumCount\":1,\"playCount\":7,\"followed\":"
        + (followed ? "true" : "false") + ",\"resolving\":false,\"artworkUrl\":\"/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-\"}";
}

QByteArray pageJson(const QList<QByteArray>& artists, const QString& cursor)
{
    QByteArray body = "{\"artists\":[";
    for (qsizetype index = 0; index < artists.size(); ++index) {
        body += (index > 0 ? "," : "") + artists.at(index);
    }
    return body + "],\"nextCursor\":\"" + cursor.toUtf8() + "\",\"totalCount\":3}";
}

} // namespace

// The account's artist list: the contract, the calls, following, and the
// Artists tab's paged list.
class AccountArtistsTest : public QObject {
    Q_OBJECT

private slots:
    void pagesAreAcceptedWholeOrNotAtAll();
    void listPathsAreBuiltSafely();
    void followGoesToTheService();
    void tabListsPagesAndLoadsMore();
    void searchWaitsForTypingToStop();
};

void AccountArtistsTest::pagesAreAcceptedWholeOrNotAtAll()
{
    const account::LibraryArtistPage page = account::parseLibraryArtistPage(
        pageJson({artistJson(QStringLiteral("a1"), QStringLiteral("Mira Sol"), true)}, QStringLiteral("next-1")));
    QVERIFY(page.valid);
    QCOMPARE(page.artists.size(), 1);
    QCOMPARE(page.artists.first().id, QStringLiteral("a1"));
    QVERIFY(page.artists.first().followed);
    QCOMPARE(page.artists.first().artworkUrl, QStringLiteral("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-"));
    QCOMPARE(page.artists.first().detailIdentity.name, QStringLiteral("Mira Sol"));
    QCOMPARE(page.nextCursor, QStringLiteral("next-1"));
    QCOMPARE(page.totalCount, std::optional<qint64>(3));

    // One bad artist refuses the whole page.
    QByteArray badCount = artistJson(QStringLiteral("a2"), QStringLiteral("B"));
    badCount.replace("\"trackCount\":3", "\"trackCount\":2.5");
    QVERIFY(!account::parseLibraryArtistPage(pageJson({artistJson(QStringLiteral("a1"), QStringLiteral("A")), badCount}, {})).valid);
    QByteArray noFollowed = artistJson(QStringLiteral("a3"), QStringLiteral("C"));
    noFollowed.replace("\"followed\":false,", "");
    QVERIFY(!account::parseLibraryArtistPage(pageJson({noFollowed}, {})).valid);
    QByteArray unsafeArt = artistJson(QStringLiteral("a4"), QStringLiteral("D"));
    unsafeArt.replace("/v1/music/art/AbCdEfGhIjKlMnOpQrSt_-", "javascript:alert(1)");
    QVERIFY(!account::parseLibraryArtistPage(pageJson({unsafeArt}, {})).valid);
    QVERIFY(!account::parseLibraryArtistPage("{\"artists\":{}}").valid);
    QVERIFY(account::parseLibraryArtistPage("{\"data\":{\"artists\":[],\"nextCursor\":null}}").valid);
    QVERIFY(account::isValidLibraryArtistPublicId(QStringLiteral("a/1 b")));
    QVERIFY(!account::isValidLibraryArtistPublicId(QStringLiteral("a\nb")));
    QVERIFY(!account::isValidLibraryArtistPublicId(QString()));
}

void AccountArtistsTest::listPathsAreBuiltSafely()
{
    account::LibraryArtistListRequest request;
    request.view = account::LibraryArtistView::Followed;
    request.query = QStringLiteral("  mira sol&co ");
    request.cursor = QStringLiteral("c/1");
    QCOMPARE(account::libraryArtistListPath(request),
             std::optional<QString>(QStringLiteral("/v1/music/library/artists?view=followed&limit=50&q=mira%20sol%26co&cursor=c%2F1")));
    request.limit = 101;
    QVERIFY(!account::libraryArtistListPath(request));
    request.limit = 10;
    request.query = QString(121, QLatin1Char('x'));
    QVERIFY(!account::libraryArtistListPath(request));
}

void AccountArtistsTest::followGoesToTheService()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    lmp::core::db::FollowedArtistRepository followed(modes.library.database());
    lmp::core::UserDataOperationGate gate;
    lmp::core::ArtistFollowService follows(
        {modes.session.client, modes.session.service, modes.modes, followed, gate});
    lmp::core::FollowableArtist artist;
    artist.publicId = QStringLiteral("a 1");
    artist.identity.name = QStringLiteral("Mira Sol");
    QVERIFY(follows.canFollow(artist)); // on this PC: by name
    QVERIFY(modes.validate(QStringLiteral("owner-a")));
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    QVERIFY(follows.canFollow(artist));
    lmp::core::FollowableArtist noId = artist;
    noId.publicId.clear();
    QVERIFY(!follows.canFollow(noId));

    follows.rememberAccountArtist(artist.publicId, false);
    modes.session.transport.script(kArtistsPath + QStringLiteral("/a 1/follow"), FakeHttpTransport::ok("{}"));
    QFuture<Result<bool>> done = follows.setFollowed(artist, true);
    QTRY_VERIFY(done.isFinished());
    QCOMPARE(done.result().value(), true);
    QVERIFY(follows.isFollowed(artist));
    const lmp::core::net::HttpRequest& put = modes.session.transport.requests.last();
    QCOMPARE(put.method, lmp::core::net::HttpMethod::Put);
    QCOMPARE(put.url.toEncoded().right(17), QByteArray("artists/a%201/follow").right(17));
    QVERIFY(followed.loadFollowedArtists(QStringLiteral("local")).isEmpty()); // not the list on this PC

    modes.session.transport.script(kArtistsPath + QStringLiteral("/a 1/follow"),
                                   FakeHttpTransport::failed(FailureKind::Server, 503));
    QFuture<Result<bool>> refused = follows.setFollowed(artist, false);
    QTRY_VERIFY(refused.isFinished());
    QVERIFY(!refused.result().isOk());
    QVERIFY(follows.isFollowed(artist)); // unchanged
}

void AccountArtistsTest::tabListsPagesAndLoadsMore()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    lmp::core::db::FollowedArtistRepository followed(modes.library.database());
    lmp::core::UserDataOperationGate gate;
    lmp::core::ArtistFollowService follows(
        {modes.session.client, modes.session.service, modes.modes, followed, gate});
    lmp::ui::Navigator navigator;
    lmp::ui::AccountArtistsViewModel tab({modes.session.client, modes.session.service, modes.modes, follows, navigator});
    QVERIFY(!tab.active());
    QVERIFY(modes.validate(QStringLiteral("owner-a")));
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    QVERIFY(tab.active());

    modes.session.transport.script(
        kArtistsPath, FakeHttpTransport::ok(pageJson({artistJson(QStringLiteral("a1"), QStringLiteral("Mira Sol"), true),
                                                      artistJson(QStringLiteral("a2"), QStringLiteral("Night Owls"))},
                                                     QStringLiteral("next-1"))));
    tab.reload();
    QTRY_COMPARE(tab.artists()->count(), 2);
    QVERIFY(tab.hasMore());
    QVERIFY(!tab.loading());
    QCOMPARE(tab.artists()->cardAt(0).subtitle, QStringLiteral("3 songs - 1 album"));
    QCOMPARE(tab.artists()->cardAt(0).artworkUrl.scheme(), QStringLiteral("image"));

    // The next page is added; a repeated artist is not shown twice.
    modes.session.transport.script(
        kArtistsPath, FakeHttpTransport::ok(pageJson({artistJson(QStringLiteral("a2"), QStringLiteral("Night Owls")),
                                                      artistJson(QStringLiteral("a3"), QStringLiteral("Canyon"))},
                                                     QString())));
    tab.loadMore();
    QTRY_COMPARE(tab.artists()->count(), 3);
    QVERIFY(!tab.hasMore());
    QCOMPARE(QUrlQuery(modes.session.transport.requests.last().url).queryItemValue(QStringLiteral("cursor")),
             QStringLiteral("next-1"));

    tab.openCard(2);
    QCOMPARE(navigator.current().param(QStringLiteral("key")), QStringLiteral("Canyon"));
    QCOMPARE(navigator.current().param(QStringLiteral("artistId")), QStringLiteral("a3"));

    // A refused page says so and keeps what is shown.
    modes.session.transport.script(kArtistsPath, FakeHttpTransport::ok("{\"artists\":7}"));
    tab.reload();
    QTRY_VERIFY(!tab.status().isEmpty());
    QCOMPARE(tab.artists()->count(), 3);

    // Leaving Account mode empties the tab.
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::LocalOnly));
    QVERIFY(!tab.active());
    QCOMPARE(tab.artists()->count(), 0);
}

void AccountArtistsTest::searchWaitsForTypingToStop()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    lmp::core::db::FollowedArtistRepository followed(modes.library.database());
    lmp::core::UserDataOperationGate gate;
    lmp::core::ArtistFollowService follows(
        {modes.session.client, modes.session.service, modes.modes, followed, gate});
    lmp::ui::Navigator navigator;
    lmp::ui::AccountArtistsViewModel tab({modes.session.client, modes.session.service, modes.modes, follows, navigator});
    QVERIFY(modes.validate(QStringLiteral("owner-a")));
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::Account));
    const int before = modes.session.transport.requestCount(kArtistsPath);
    tab.setQuery(QStringLiteral("m"));
    tab.setQuery(QStringLiteral("mi"));
    tab.setQuery(QStringLiteral("mira"));
    QTest::qWait(100);
    QCOMPARE(modes.session.transport.requestCount(kArtistsPath), before);
    QTRY_COMPARE(modes.session.transport.requestCount(kArtistsPath), before + 1);
    QCOMPARE(QUrlQuery(modes.session.transport.requests.last().url).queryItemValue(QStringLiteral("q")),
             QStringLiteral("mira"));
}

QTEST_MAIN(AccountArtistsTest)
#include "tst_account_artists.moc"
