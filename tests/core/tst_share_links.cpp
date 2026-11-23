#include "core/share/ShareLinks.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "CatalogFixtures.h"

namespace share = lmp::core::share;
using lmp::test::json;

namespace {

lmp::core::Track remoteSong(const QString& id)
{
    lmp::core::Track track;
    track.title = QStringLiteral("Song ") + id;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceTag = QStringLiteral("tag-b");
    track.remoteId = QStringLiteral("tag-b:") + id;
    track.sourceUrl = QStringLiteral("https://music.example.test/") + id;
    track.durationSeconds = 200;
    return track;
}

QJsonObject object(const QByteArray& body)
{
    return QJsonDocument::fromJson(body).object();
}

} // namespace

class ShareLinksTest : public QObject {
    Q_OBJECT

private slots:
    void songBodyCarriesTheTrack();
    void listBodyNeedsSongsAndATitle();
    void artistBodyNeedsAnIdentity();
    void linkMustBeHttps();
    void sharedItemsAreRead();
    void codesComeOnlyFromTheAppsOwnLinks();
    void textNamesTheItem();
};

void ShareLinksTest::songBodyCarriesTheTrack()
{
    const std::optional<QByteArray> body =
        share::shareRequestBody({.kind = share::ShareKind::Song, .track = remoteSong(QStringLiteral("1"))});
    QVERIFY(body.has_value());
    const QJsonObject root = object(*body);
    QCOMPARE(root.value(QStringLiteral("kind")).toString(), QStringLiteral("song"));
    QCOMPARE(root.value(QStringLiteral("track")).toObject().value(QStringLiteral("title")).toString(),
             QStringLiteral("Song 1"));
    // A song on this PC cannot be shared.
    lmp::core::Track local;
    local.title = QStringLiteral("Local");
    local.filePath = QStringLiteral("C:/Music/local.mp3");
    QVERIFY(!share::shareRequestBody({.kind = share::ShareKind::Song, .track = local}).has_value());
}

void ShareLinksTest::listBodyNeedsSongsAndATitle()
{
    const std::optional<QByteArray> body = share::shareRequestBody({.kind = share::ShareKind::List,
                                                                    .title = QStringLiteral("An Album"),
                                                                    .subtitle = QStringLiteral("Artist"),
                                                                    .artworkUrl = QStringLiteral("https://art.example.test/a.jpg"),
                                                                    .tracks = {remoteSong(QStringLiteral("1")),
                                                                               remoteSong(QStringLiteral("2"))}});
    QVERIFY(body.has_value());
    const QJsonObject root = object(*body);
    QCOMPARE(root.value(QStringLiteral("kind")).toString(), QStringLiteral("list"));
    QCOMPARE(root.value(QStringLiteral("tracks")).toArray().size(), 2);
    QCOMPARE(root.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Artist"));
    QVERIFY(!share::shareRequestBody({.kind = share::ShareKind::List, .title = QStringLiteral("Empty")}).has_value());
    QVERIFY(!share::shareRequestBody(
                 {.kind = share::ShareKind::List, .tracks = {remoteSong(QStringLiteral("1"))}})
                 .has_value());
}

void ShareLinksTest::artistBodyNeedsAnIdentity()
{
    share::ShareRequest request{.kind = share::ShareKind::Artist};
    request.artist = {.catalogId = QStringLiteral("123"), .sourceUrl = {}, .name = QStringLiteral("Someone"),
                      .provider = QStringLiteral("tag-a")};
    const std::optional<QByteArray> body = share::shareRequestBody(request);
    QVERIFY(body.has_value());
    const QJsonObject root = object(*body);
    QCOMPARE(root.value(QStringLiteral("kind")).toString(), QStringLiteral("artist"));
    QCOMPARE(root.value(QStringLiteral("catalogId")).toString(), QStringLiteral("123"));
    QVERIFY(!root.contains(QStringLiteral("sourceUrl")));
    request.artist.catalogId.clear();
    QVERIFY(!share::shareRequestBody(request).has_value());
}

void ShareLinksTest::linkMustBeHttps()
{
    const std::optional<share::ShareLink> link =
        share::parseShareLink(json("{'code':'Ab12Cd34Ef','url':'https://app.example.test/s/Ab12Cd34Ef','title':'T'}"));
    QVERIFY(link.has_value());
    QCOMPARE(link->code, QStringLiteral("Ab12Cd34Ef"));
    QVERIFY(!share::parseShareLink(json("{'url':'http://app.example.test/s/Ab12Cd34Ef'}")).has_value());
    QVERIFY(!share::parseShareLink(json("{'code':'x'}")).has_value());
    QVERIFY(!share::parseShareLink(QByteArray("nope")).has_value());
}

void ShareLinksTest::sharedItemsAreRead()
{
    const std::optional<share::SharedItem> list = share::parseSharedItem(
        json("{'kind':'list','title':'Mix','subtitle':'Sub','tracks':[{'id':'tag-b:1','provider':'tag-b','title':'One',"
             "'sourceUrl':'https://music.example.test/1'}]}"),
        QStringLiteral("Ab12Cd34Ef"));
    QVERIFY(list.has_value());
    QCOMPARE(list->kind, share::ShareKind::List);
    QCOMPARE(list->code, QStringLiteral("Ab12Cd34Ef"));
    QCOMPARE(list->tracks.size(), 1);
    const std::optional<share::SharedItem> song = share::parseSharedItem(
        json("{'kind':'song','title':'One','track':{'id':'tag-b:1','provider':'tag-b','title':'One',"
             "'sourceUrl':'https://music.example.test/1'}}"),
        QStringLiteral("Ab12Cd34Ef"));
    QVERIFY(song.has_value());
    QCOMPARE(song->tracks.size(), 1);
    const std::optional<share::SharedItem> artist = share::parseSharedItem(
        json("{'kind':'artist','title':'Someone','artist':{'provider':'tag-a','catalogId':'123'}}"),
        QStringLiteral("Ab12Cd34Ef"));
    QVERIFY(artist.has_value());
    QVERIFY(artist->artist.has_value());
    QCOMPARE(artist->artist->name, QStringLiteral("Someone"));
    QCOMPARE(artist->artist->catalogId, QStringLiteral("123"));
    QVERIFY(!share::parseSharedItem(json("{'kind':'video'}"), QStringLiteral("Ab12Cd34Ef")).has_value());
}

void ShareLinksTest::codesComeOnlyFromTheAppsOwnLinks()
{
    const QString origin = QStringLiteral("https://app.example.test");
    QCOMPARE(share::shareCodeFromLink(QStringLiteral(" https://app.example.test/s/Ab12Cd34Ef "), origin),
             std::optional<QString>(QStringLiteral("Ab12Cd34Ef")));
    QCOMPARE(share::shareCodeFromLink(QStringLiteral("https://APP.example.test/s/Ab12Cd34Ef/"), origin),
             std::optional<QString>(QStringLiteral("Ab12Cd34Ef")));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("https://other.example.test/s/Ab12Cd34Ef"), origin));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("http://app.example.test/s/Ab12Cd34Ef"), origin));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("https://app.example.test/s/short"), origin));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("https://app.example.test/x/Ab12Cd34Ef"), origin));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("arijit singh"), origin));
    QVERIFY(!share::shareCodeFromLink(QStringLiteral("https://app.example.test/s/Ab12Cd34Ef"), QString()));
    QVERIFY(share::isShareCode(QStringLiteral("0123456789")));
    QVERIFY(!share::isShareCode(QStringLiteral("01234-6789")));
}

void ShareLinksTest::textNamesTheItem()
{
    QCOMPARE(share::shareText({.kind = share::ShareKind::Song, .track = remoteSong(QStringLiteral("1"))}),
             QStringLiteral("Song 1 by Artist"));
    QCOMPARE(share::shareText({.kind = share::ShareKind::List, .title = QStringLiteral("A"), .subtitle = QStringLiteral("B")}),
             QStringLiteral("A \u00b7 B"));
    QCOMPARE(share::shareText({.kind = share::ShareKind::List, .title = QStringLiteral("A")}), QStringLiteral("A"));
}

QTEST_GUILESS_MAIN(ShareLinksTest)
#include "tst_share_links.moc"
