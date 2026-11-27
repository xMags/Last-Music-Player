#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include "ui/navigation/Navigator.h"
#include "ui/navigation/RouteRules.h"

using lmp::ui::Navigator;
using lmp::ui::Route;
using lmp::ui::RouteRules;
using lmp::ui::SourceIdentity;

// The route-rule cases are the data-level cases of the other clients' route
// tests; address strings and URL decoding do not exist here. Source tags are
// made-up words (`tag-a`), never real ones.
class NavigatorTest : public QObject {
    Q_OBJECT

private slots:
    void startsAtHome();
    void pushAndBack();
    void replaceKeepsDepth();
    void scrollOffsetIsKeptPerEntry();
    void theReturnOffsetOutlivesAReload();
    void pageViewModelBelongsToItsEntry();
    void openViewOnlyOpensMainViews();
    void startingOverKeepsAMainViewInPlace();
    void startingOverLeavesASourcePageForHome();

    void catalogDetailNeedsAKnownTypeAndAnId();
    void chartRulesFollowTheScope();
    void sourceArtistNeedsNameAndNumericCatalogId();
    void sourceAlbumMayBeNamedByIdAlone();
    void libraryArtistNeedsAPublicId();
    void chartListsAndShelvesAreKnownIds();
    void externalPlaylistIdsNeedThePrefix();
    void shareCodesAreTenLettersOrDigits();
    void artistSectionsAreKnownIds();
};

void NavigatorTest::startsAtHome()
{
    const Navigator navigator;
    QCOMPARE(navigator.current().kind(), Route::Kind::Home);
    QVERIFY(!navigator.canGoBack());
}

void NavigatorTest::pushAndBack()
{
    Navigator navigator;
    QSignalSpy changed(&navigator, &Navigator::currentChanged);
    navigator.push(Route(Route::Kind::Browse));
    navigator.push(Route(Route::Kind::Library));
    QCOMPARE(navigator.current().kind(), Route::Kind::Library);
    QVERIFY(!navigator.returning());
    QVERIFY(navigator.canGoBack());
    navigator.back();
    QCOMPARE(navigator.current().kind(), Route::Kind::Browse);
    QVERIFY(navigator.returning());
    navigator.back();
    navigator.back(); // nothing before Home
    QCOMPARE(navigator.current().kind(), Route::Kind::Home);
    QCOMPARE(changed.count(), 4);
}

void NavigatorTest::replaceKeepsDepth()
{
    Navigator navigator;
    navigator.push(Route(Route::Kind::Browse));
    navigator.replace(Route(Route::Kind::Settings));
    QCOMPARE(navigator.depth(), 2);
    QCOMPARE(navigator.current().kind(), Route::Kind::Settings);
    navigator.back();
    QCOMPARE(navigator.current().kind(), Route::Kind::Home);
}

void NavigatorTest::scrollOffsetIsKeptPerEntry()
{
    Navigator navigator;
    navigator.setCurrentScrollOffset(120);
    navigator.push(Route(Route::Kind::Library));
    QCOMPARE(navigator.currentScrollOffset(), 0.0);
    navigator.setCurrentScrollOffset(640);
    navigator.push(Route(Route::Kind::Settings));
    navigator.back();
    QCOMPARE(navigator.currentScrollOffset(), 640.0);
    navigator.back();
    QCOMPARE(navigator.currentScrollOffset(), 120.0);
    navigator.setCurrentScrollOffset(-5);
    QCOMPARE(navigator.currentScrollOffset(), 0.0);
}

void NavigatorTest::pageViewModelBelongsToItsEntry()
{
    Navigator navigator;
    navigator.push(Route(Route::Kind::Library));
    QPointer<QObject> page = new QObject();
    navigator.setCurrentPage(page);
    QCOMPARE(page->parent(), &navigator);
    navigator.push(Route(Route::Kind::Settings));
    QCOMPARE(navigator.currentPage(), nullptr);
    navigator.back();
    QCOMPARE(navigator.currentPage(), page.data());
    navigator.back();
    QTRY_VERIFY(page.isNull()); // deleted once its entry left the history
}

void NavigatorTest::openViewOnlyOpensMainViews()
{
    Navigator navigator;
    navigator.openView(static_cast<int>(Route::Kind::Settings));
    QCOMPARE(navigator.current().kind(), Route::Kind::Settings);
    navigator.openView(static_cast<int>(Route::Kind::Chart));
    QCOMPARE(navigator.current().kind(), Route::Kind::Settings);
}

void NavigatorTest::startingOverKeepsAMainViewInPlace()
{
    Navigator navigator;
    navigator.push(Route(Route::Kind::CatalogDetail));
    QPointer<QObject> old = new QObject();
    navigator.setCurrentPage(old);
    navigator.push(Route(Route::Kind::Settings));
    navigator.setCurrentScrollOffset(300);
    QSignalSpy changed(&navigator, &Navigator::currentChanged);
    QSignalSpy history(&navigator, &Navigator::historyChanged);

    navigator.startOver();
    QCOMPARE(navigator.current().kind(), Route::Kind::Settings);
    QCOMPARE(navigator.depth(), 1);
    QVERIFY(!navigator.canGoBack());
    QCOMPARE(navigator.currentScrollOffset(), 300.0);
    QCOMPARE(changed.count(), 0); // the page on screen is not shown again
    QCOMPARE(history.count(), 1);
    QTRY_VERIFY(old.isNull());

    navigator.startOver(); // nothing behind it: nothing changes
    QCOMPARE(history.count(), 1);
}

void NavigatorTest::startingOverLeavesASourcePageForHome()
{
    Navigator navigator;
    navigator.push(Route(Route::Kind::Library));
    navigator.push(Route(Route::Kind::Chart));
    QPointer<QObject> page = new QObject();
    navigator.setCurrentPage(page);
    QSignalSpy changed(&navigator, &Navigator::currentChanged);

    navigator.startOver();
    QCOMPARE(navigator.current().kind(), Route::Kind::Home);
    QCOMPARE(navigator.depth(), 1);
    QVERIFY(!navigator.returning());
    QCOMPARE(changed.count(), 1);
    QTRY_VERIFY(page.isNull());
}

void NavigatorTest::catalogDetailNeedsAKnownTypeAndAnId()
{
    const RouteRules rules;
    const auto detail = rules.catalogDetail(QStringLiteral("album"), QStringLiteral("album/one"));
    QVERIFY(detail.has_value());
    QCOMPARE(detail->param(QStringLiteral("catalogId")), QStringLiteral("album/one"));
    QVERIFY(!rules.catalogDetail(QStringLiteral("video"), QStringLiteral("1")).has_value());
    QVERIFY(!rules.catalogDetail(QStringLiteral("artist"), QStringLiteral("  ")).has_value());
}

void NavigatorTest::chartRulesFollowTheScope()
{
    const RouteRules rules;
    const auto city = rules.chart(QStringLiteral("song"), QStringLiteral("city"),
                                  QStringLiteral("city.1"), QStringLiteral("GB"));
    QVERIFY(city.has_value());
    QCOMPARE(city->param(QStringLiteral("region")), QStringLiteral("gb"));
    QVERIFY(rules.chart(QStringLiteral("song"), QStringLiteral("city"), QStringLiteral("city.1")));
    QVERIFY(rules.chart(QStringLiteral("song"), QStringLiteral("genre"), QStringLiteral("14")));
    QVERIFY(rules.chart(QStringLiteral("song"), QStringLiteral("global"), QStringLiteral("top-songs")));
    QVERIFY(rules.chart(QStringLiteral("album"), QStringLiteral("global"), QStringLiteral("top-albums")));
    QVERIFY(!rules.chart(QStringLiteral("song"), QStringLiteral("global"), QStringLiteral("foo")));
    QVERIFY(!rules.chart(QStringLiteral("song"), QStringLiteral("city"), QStringLiteral("city.1"),
                         QStringLiteral("usa")));
    QVERIFY(!rules.chart(QStringLiteral("song"), QStringLiteral("genre"), QStringLiteral("14"),
                         QStringLiteral("gb")));
    QVERIFY(!rules.chart(QStringLiteral("album"), QStringLiteral("city"), QStringLiteral("city.1")));
    QVERIFY(!rules.chart(QStringLiteral("song"), QStringLiteral("city"), QStringLiteral("city/1")));
    QVERIFY(!rules.chart(QStringLiteral("artist"), QStringLiteral("global"), QStringLiteral("top-artists")));
    QVERIFY(!rules.chart(QStringLiteral("song"), QStringLiteral("genre"), QString(161, QLatin1Char('a'))));
}

void NavigatorTest::sourceArtistNeedsNameAndNumericCatalogId()
{
    const RouteRules rules;
    const SourceIdentity identity{QStringLiteral("Primary Artist"), QStringLiteral("tag-a"),
                                  QStringLiteral("UC-primary"),
                                  QStringLiteral("https://example.com/browse/UC-primary"), {}};
    const auto route = rules.sourceArtist(identity, QStringLiteral("12345"));
    QVERIFY(route.has_value());
    QCOMPARE(route->param(QStringLiteral("catalogArtistId")), QStringLiteral("12345"));
    QCOMPARE(route->param(QStringLiteral("sourceUrl")),
             QStringLiteral("https://example.com/browse/UC-primary"));
    QVERIFY(!rules.sourceArtist(identity, QStringLiteral("UC-invalid")).has_value());
    // An http link is dropped, not fatal.
    const auto plain = rules.sourceArtist(
        {QStringLiteral("A"), QStringLiteral("tag-a"), {}, QStringLiteral("http://example.com"), {}});
    QVERIFY(plain.has_value());
    QVERIFY(plain->param(QStringLiteral("sourceUrl")).isEmpty());
    // An artist always needs its name.
    QVERIFY(!rules.sourceArtist({{}, QStringLiteral("tag-a"), QStringLiteral("UC1"), {}, {}}));
}

void NavigatorTest::sourceAlbumMayBeNamedByIdAlone()
{
    const RouteRules rules;
    const auto byId = rules.sourceAlbum({{}, QStringLiteral("tag-a"), QStringLiteral("rel_x"), {}, {}});
    QVERIFY(byId.has_value());
    QCOMPARE(byId->param(QStringLiteral("catalogId")), QStringLiteral("rel_x"));
    const auto byName = rules.sourceAlbum(
        {QStringLiteral("Greatest Hits"), QStringLiteral("tag-a"), {}, {}, QStringLiteral("Artist A")});
    QVERIFY(byName.has_value());
    QCOMPARE(byName->param(QStringLiteral("artist")), QStringLiteral("Artist A"));
    QVERIFY(!rules.sourceAlbum({{}, QStringLiteral("tag-a"), {}, {}, {}}));
    QVERIFY(!rules.sourceAlbum({QStringLiteral("A"), {}, {}, {}, {}}));
}

void NavigatorTest::libraryArtistNeedsAPublicId()
{
    const RouteRules rules;
    QVERIFY(rules.libraryArtist(QStringLiteral("mar_11111111-1111-4111-8111-111111111111")));
    QVERIFY(!rules.libraryArtist(QStringLiteral("42")));
}

void NavigatorTest::chartListsAndShelvesAreKnownIds()
{
    const RouteRules rules;
    QVERIFY(rules.charts(QStringLiteral("explore")));
    QVERIFY(rules.charts(QStringLiteral("cities")));
    QVERIFY(!rules.charts(QStringLiteral("indian-cities")));
    QVERIFY(!rules.charts(QStringLiteral("bogus")));
    for (const char* shelf : {"moods-activities", "new-release-songs", "new-releases",
                              "biggest-hits", "popular-artists"}) {
        QVERIFY2(rules.shelf(QLatin1String(shelf)), shelf);
    }
    QVERIFY(!rules.shelf(QStringLiteral("unknown")));
    QVERIFY(!rules.shelf(QStringLiteral("toString")));
}

void NavigatorTest::externalPlaylistIdsNeedThePrefix()
{
    const RouteRules none;
    QVERIFY(!none.externalPlaylist(QStringLiteral("XPabcdefghij")));
    const RouteRules rules(QStringLiteral("XP"));
    QVERIFY(rules.externalPlaylist(QStringLiteral("XPabcdefghij")));
    QVERIFY(rules.externalPlaylist(QStringLiteral("XP") + QString(80, QLatin1Char('a'))));
    QVERIFY(!rules.externalPlaylist(QStringLiteral("abcdefghijkl"))); // keeps its prefix
    QVERIFY(!rules.externalPlaylist(QStringLiteral("XPshort")));
    QVERIFY(!rules.externalPlaylist(QStringLiteral("XP") + QString(81, QLatin1Char('a'))));
    QVERIFY(!rules.externalPlaylist(QStringLiteral("XPabcdefgh/../secret")));
    QVERIFY(!rules.externalPlaylist(QStringLiteral("XPabcdefgh spaced id")));
}

void NavigatorTest::shareCodesAreTenLettersOrDigits()
{
    const RouteRules rules;
    QVERIFY(rules.shared(QStringLiteral("AbCdEf2345")));
    QVERIFY(!rules.shared(QStringLiteral("short")));
    QVERIFY(!rules.shared(QStringLiteral("AbCd/F2345")));
}

void NavigatorTest::artistSectionsAreKnownIds()
{
    const RouteRules rules;
    const SourceIdentity identity{QStringLiteral("A"), QStringLiteral("tag-a"), {}, {}, {}};
    const auto route = rules.artistSection(identity, {}, QStringLiteral("similar-artists"),
                                           QStringLiteral("gb"));
    QVERIFY(route.has_value());
    QCOMPARE(route->param(QStringLiteral("region")), QStringLiteral("gb"));
    QVERIFY(!rules.artistSection(identity, {}, QStringLiteral("latest-release")));
    QVERIFY(!rules.artistSection(identity, QStringLiteral("bad"), QStringLiteral("albums")));
    // An unusable region is left out.
    const auto noRegion = rules.artistSection(identity, {}, QStringLiteral("albums"),
                                              QStringLiteral("usa"));
    QVERIFY(noRegion.has_value());
    QVERIFY(noRegion->param(QStringLiteral("region")).isEmpty());
}

void NavigatorTest::theReturnOffsetOutlivesAReload()
{
    Navigator navigator;
    navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("key"), QStringLiteral("a")}}));
    navigator.setCurrentScrollOffset(300);
    navigator.push(Route(Route::Kind::Collection, {{QStringLiteral("key"), QStringLiteral("b")}}));
    QCOMPARE(navigator.returnScrollOffset(), 0.0);
    navigator.back();
    QCOMPARE(navigator.returnScrollOffset(), 300.0);
    // The same page shows the playlist again: its list starts at the top
    // and says so before it is scrolled back.
    navigator.setCurrentScrollOffset(0);
    QCOMPARE(navigator.returnScrollOffset(), 300.0);
}

QTEST_GUILESS_MAIN(NavigatorTest)
#include "tst_navigator.moc"
