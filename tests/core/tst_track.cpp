#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QTest>

#include "core/model/Track.h"

using lmp::core::Track;

namespace {

Track localTrack()
{
    Track track;
    track.title = QStringLiteral("Morning Light");
    track.artist = QStringLiteral("The Test Band");
    track.album = QStringLiteral("First Record");
    track.genre = QStringLiteral("Folk");
    track.filePath = QStringLiteral("D:\\Music\\Test\\01 Morning Light.flac");
    track.dateAdded = QStringLiteral("2025-03-01");
    track.durationText = QStringLiteral("3:41");
    track.sourceKind = QStringLiteral("local");
    track.origin = QStringLiteral("local");
    track.sourceLabel = QStringLiteral("On this PC");
    track.durationSeconds = 221.5;
    track.dateAddedSortKey = 20250301;
    track.liked = true;
    return track;
}

} // namespace

class TrackTest : public QObject {
    Q_OBJECT

private slots:
    void localRoundTrip();
    void snapshotUsesTheLegacyKeys();
    void remoteDropsFilePathAndUnsafeAddresses();
    void legacyHomeHistoryEntryParses();
    void missingAndWrongTypedValuesFallBack();
    void downloadableOnlyForStreamedRemoteSongs();
    void sourceBadgeOnlyForRemoteLabels();
};

void TrackTest::localRoundTrip()
{
    const Track original = localTrack();
    const Track back = Track::fromSnapshotJson(original.toSnapshotJson());
    QCOMPARE(back, original);
}

void TrackTest::snapshotUsesTheLegacyKeys()
{
    QStringList keys = localTrack().toSnapshotJson().keys();
    keys.sort();
    QStringList expected{
        QStringLiteral("title"),          QStringLiteral("artist"),         QStringLiteral("album"),
        QStringLiteral("genre"),          QStringLiteral("filePath"),       QStringLiteral("artworkUrl"),
        QStringLiteral("dateAdded"),      QStringLiteral("duration"),       QStringLiteral("sourceKind"),
        QStringLiteral("provider"),       QStringLiteral("remoteProvider"), QStringLiteral("sourceUrl"),
        QStringLiteral("sourceLabel"),    QStringLiteral("remoteId"),       QStringLiteral("remoteCatalogId"),
        QStringLiteral("albumCatalogId"), QStringLiteral("artistCatalogId"), QStringLiteral("durationSeconds"),
        QStringLiteral("dateAddedSortKey"), QStringLiteral("isLiked"),
    };
    expected.sort();
    QCOMPARE(keys, expected);
    const QJsonObject json = localTrack().toSnapshotJson();
    QVERIFY(json.value(QStringLiteral("durationSeconds")).isDouble());
    QVERIFY(json.value(QStringLiteral("isLiked")).isBool());
    QVERIFY(json.value(QStringLiteral("duration")).isString());
}

void TrackTest::remoteDropsFilePathAndUnsafeAddresses()
{
    Track track;
    track.title = QStringLiteral("Night Drive");
    track.sourceKind = QStringLiteral("Remote");
    track.filePath = QStringLiteral("C:\\cache\\song.m4a");
    track.artworkUrl = QStringLiteral("https://images.example.test/cover.jpg");
    track.sourceUrl = QStringLiteral("https://media.example.test/song?signature=abc");
    track.sourceTag = QStringLiteral("tag-a");
    track.remoteId = QStringLiteral("song-0001");
    track.catalogId = QStringLiteral("cat-0001");

    const QJsonObject json = track.toSnapshotJson();
    QCOMPARE(json.value(QStringLiteral("filePath")).toString(), QString());
    QCOMPARE(json.value(QStringLiteral("artworkUrl")).toString(), track.artworkUrl);
    QCOMPARE(json.value(QStringLiteral("sourceUrl")).toString(), QString());
    QCOMPARE(json.value(QStringLiteral("remoteProvider")).toString(), QStringLiteral("tag-a"));
    QCOMPARE(json.value(QStringLiteral("remoteCatalogId")).toString(), QStringLiteral("cat-0001"));

    // An unsafe artwork address is cleared too; a local song keeps both.
    track.artworkUrl = QStringLiteral("http://images.example.test/cover.jpg");
    QCOMPARE(track.toSnapshotJson().value(QStringLiteral("artworkUrl")).toString(), QString());
    Track local = localTrack();
    local.artworkUrl = QStringLiteral("http://images.example.test/cover.jpg");
    QCOMPARE(local.toSnapshotJson().value(QStringLiteral("artworkUrl")).toString(), local.artworkUrl);
}

void TrackTest::legacyHomeHistoryEntryParses()
{
    // An entry as old versions wrote it into HomeHistory: no sourceKind, a web
    // address as the path, extra keys this reader ignores.
    const QByteArray text = R"({"title":"Old Favourite","artist":"Someone","album":"Album","genre":"",)"
                            R"("filePath":"https://media.example.test/old","artworkUrl":"","dateAdded":"",)"
                            R"("duration":"4:02","provider":"server","sourceUrl":"https://media.example.test/old",)"
                            R"("durationSeconds":242,"dateAddedSortKey":0,"playCount":7,"lastPlayedOrder":3,)"
                            R"("isLiked":false,"key":"old-key"})";
    const Track track = Track::fromSnapshotJson(QJsonDocument::fromJson(text).object());
    QCOMPARE(track.title, QStringLiteral("Old Favourite"));
    QCOMPARE(track.sourceKind, QStringLiteral("remote"));
    QCOMPARE(track.sourceLabel, QStringLiteral("Remote"));
    QCOMPARE(track.origin, QStringLiteral("server"));
    QCOMPARE(track.durationText, QStringLiteral("4:02"));
    QCOMPARE(track.durationSeconds, 242.0);
    QVERIFY(!track.liked);
    QVERIFY(track.isRemote());
}

void TrackTest::missingAndWrongTypedValuesFallBack()
{
    const Track empty = Track::fromSnapshotJson(QJsonObject());
    QCOMPARE(empty.sourceKind, QStringLiteral("local"));
    QCOMPARE(empty.sourceLabel, QStringLiteral("Local"));
    QCOMPARE(empty.durationSeconds, 0.0);

    QJsonObject odd;
    odd.insert(QStringLiteral("title"), 5);
    odd.insert(QStringLiteral("durationSeconds"), QStringLiteral("12"));
    odd.insert(QStringLiteral("isLiked"), 1);
    const Track track = Track::fromSnapshotJson(odd);
    QCOMPARE(track.title, QString());
    QCOMPARE(track.durationSeconds, 0.0);
    QVERIFY(!track.liked);
}

void TrackTest::downloadableOnlyForStreamedRemoteSongs()
{
    Track track;
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("https://media.example.test/a");
    QVERIFY(track.isDownloadable());
    track.sourceKind = QStringLiteral("Remote");
    QVERIFY(track.isDownloadable());
    track.sourceKind = QStringLiteral("REMOTE");
    QVERIFY(!track.isDownloadable());
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("D:\\a.mp3");
    QVERIFY(!track.isDownloadable());
    QVERIFY(!localTrack().isDownloadable());
}

void TrackTest::sourceBadgeOnlyForRemoteLabels()
{
    Track track;
    QCOMPARE(track.sourceBadgeText(), QString());
    for (const char* local : {"Local", "On this PC", "Manual", "Album", "Playlist", "System", "Auto mix"}) {
        track.sourceLabel = QString::fromLatin1(local);
        QCOMPARE(track.sourceBadgeText(), QString());
    }
    track.sourceLabel = QStringLiteral("Cloud");
    QCOMPARE(track.sourceBadgeText(), QStringLiteral("CLOUD"));
}

QTEST_GUILESS_MAIN(TrackTest)
#include "tst_track.moc"
