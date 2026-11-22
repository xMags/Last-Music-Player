#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/db/AccountTrackJson.h"
#include "core/model/Track.h"

#include <optional>

using lmp::core::Track;

namespace json = lmp::core::db::account_track_json;

namespace {

Track fullSong()
{
    Track track;
    track.remoteId = QStringLiteral("track-a");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("server");
    track.sourceTag = QStringLiteral("source-a");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/track-a");
    track.title = QStringLiteral("Track A");
    track.artist = QStringLiteral("Artist A");
    track.album = QStringLiteral("Album A");
    track.genre = QStringLiteral("Electronic");
    track.artworkUrl = QStringLiteral("https://images.example.test/track-a.jpg");
    track.catalogId = QStringLiteral("catalog-track-a");
    track.albumCatalogId = QStringLiteral("catalog-album-a");
    track.artistCatalogId = QStringLiteral("catalog-artist-a");
    track.dateAdded = QStringLiteral("2026-08-23");
    track.durationText = QStringLiteral("4:05");
    track.durationSeconds = 245.5;
    track.dateAddedSortKey = 1787000000;
    track.liked = true;
    return track;
}

QJsonObject parse(const QString& text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

} // namespace

class AccountTrackJsonTest : public QObject {
    Q_OBJECT

private slots:
    void writesEveryKeyWithItsType();
    void providerFallsBackToOrigin();
    void addressesAreCleaned();
    void roundTripsThroughText();
    void readsOlderKeysAndDefaults();
    void refusesTextThatIsNotAnObject();
};

void AccountTrackJsonTest::writesEveryKeyWithItsType()
{
    const QJsonObject object = parse(json::serialize(fullSong()));
    QStringList keys = object.keys();
    keys.sort();
    QStringList expected{QStringLiteral("album"),           QStringLiteral("albumCatalogId"), QStringLiteral("artist"),
                         QStringLiteral("artistCatalogId"), QStringLiteral("artworkUrl"),     QStringLiteral("catalogId"),
                         QStringLiteral("dateAdded"),       QStringLiteral("dateAddedSortKey"), QStringLiteral("duration"),
                         QStringLiteral("durationSec"),     QStringLiteral("durationSeconds"), QStringLiteral("genre"),
                         QStringLiteral("id"),              QStringLiteral("isLiked"),        QStringLiteral("provider"),
                         QStringLiteral("remoteId"),        QStringLiteral("sourceKind"),     QStringLiteral("sourceUrl"),
                         QStringLiteral("title")};
    expected.sort();
    QCOMPARE(keys, expected);

    QCOMPARE(object.value(QStringLiteral("id")).toString(), QStringLiteral("track-a"));
    QCOMPARE(object.value(QStringLiteral("remoteId")).toString(), QStringLiteral("track-a"));
    QCOMPARE(object.value(QStringLiteral("provider")).toString(), QStringLiteral("source-a"));
    QCOMPARE(object.value(QStringLiteral("catalogId")).toString(), QStringLiteral("catalog-track-a"));
    QCOMPARE(object.value(QStringLiteral("duration")).toString(), QStringLiteral("4:05"));
    QVERIFY(object.value(QStringLiteral("durationSeconds")).isDouble());
    QCOMPARE(object.value(QStringLiteral("durationSec")).toDouble(), 245.5);
    QCOMPARE(object.value(QStringLiteral("dateAddedSortKey")).toDouble(), 1787000000.0);
    QVERIFY(object.value(QStringLiteral("isLiked")).isBool());
    QVERIFY(object.value(QStringLiteral("isLiked")).toBool());
    QVERIFY(object.value(QStringLiteral("title")).isString());
}

void AccountTrackJsonTest::providerFallsBackToOrigin()
{
    Track track = fullSong();
    track.sourceTag.clear();
    QCOMPARE(parse(json::serialize(track)).value(QStringLiteral("provider")).toString(), QStringLiteral("server"));
    track.origin = QStringLiteral("account");
    QCOMPARE(parse(json::serialize(track)).value(QStringLiteral("provider")).toString(), QString());
}

void AccountTrackJsonTest::addressesAreCleaned()
{
    Track track = fullSong();
    track.sourceUrl = QStringLiteral("https://music.example.test/a?signature=secret");
    track.artworkUrl = QStringLiteral(
        "https://relay.example.test/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg&media_token=t");
    const QJsonObject object = parse(json::serialize(track));
    QCOMPARE(object.value(QStringLiteral("sourceUrl")).toString(), QString());
    QCOMPARE(object.value(QStringLiteral("artworkUrl")).toString(), QStringLiteral("https://images.example.test/cover.jpg"));
}

void AccountTrackJsonTest::roundTripsThroughText()
{
    const Track original = fullSong();
    const std::optional<Track> read = json::deserialize(json::serialize(original));
    QVERIFY(read.has_value());
    QCOMPARE(read->remoteId, original.remoteId);
    QCOMPARE(read->sourceKind, original.sourceKind);
    QCOMPARE(read->sourceTag, original.sourceTag);
    QCOMPARE(read->sourceUrl, original.sourceUrl);
    QCOMPARE(read->title, original.title);
    QCOMPARE(read->artist, original.artist);
    QCOMPARE(read->album, original.album);
    QCOMPARE(read->genre, original.genre);
    QCOMPARE(read->artworkUrl, original.artworkUrl);
    QCOMPARE(read->catalogId, original.catalogId);
    QCOMPARE(read->albumCatalogId, original.albumCatalogId);
    QCOMPARE(read->artistCatalogId, original.artistCatalogId);
    QCOMPARE(read->dateAdded, original.dateAdded);
    QCOMPARE(read->durationText, original.durationText);
    QCOMPARE(read->durationSeconds, original.durationSeconds);
    QCOMPARE(read->dateAddedSortKey, original.dateAddedSortKey);
    QCOMPARE(read->liked, original.liked);
    // Always read back as an account song.
    QCOMPARE(read->origin, QStringLiteral("account"));
    QCOMPARE(read->sourceLabel, QStringLiteral("Account"));
    QCOMPARE(read->filePath, QString());
}

void AccountTrackJsonTest::readsOlderKeysAndDefaults()
{
    const std::optional<Track> older =
        json::deserialize(QStringLiteral("{\"remoteId\":\"r-1\",\"durationSeconds\":12,\"title\":5,\"isLiked\":\"yes\"}"));
    QVERIFY(older.has_value());
    QCOMPARE(older->remoteId, QStringLiteral("r-1"));
    QCOMPARE(older->durationSeconds, 12.0);
    QCOMPARE(older->sourceKind, QStringLiteral("remote"));
    // Mistyped fields take their defaults.
    QCOMPARE(older->title, QString());
    QVERIFY(!older->liked);

    const std::optional<Track> both = json::deserialize(
        QStringLiteral("{\"id\":\"new\",\"remoteId\":\"old\",\"durationSec\":3,\"durationSeconds\":9}"));
    QVERIFY(both.has_value());
    QCOMPARE(both->remoteId, QStringLiteral("new"));
    QCOMPARE(both->durationSeconds, 3.0);

    const std::optional<Track> empty = json::deserialize(QStringLiteral("{}"));
    QVERIFY(empty.has_value());
    QCOMPARE(empty->remoteId, QString());
}

void AccountTrackJsonTest::refusesTextThatIsNotAnObject()
{
    QVERIFY(!json::deserialize(QString()).has_value());
    QVERIFY(!json::deserialize(QStringLiteral("not json")).has_value());
    QVERIFY(!json::deserialize(QStringLiteral("[1,2]")).has_value());
    QVERIFY(!json::deserialize(QStringLiteral("{\"id\":")).has_value());
}

QTEST_GUILESS_MAIN(AccountTrackJsonTest)
#include "tst_account_track_json.moc"
