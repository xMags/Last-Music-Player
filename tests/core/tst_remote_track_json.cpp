#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/remote/TrackJson.h"

namespace track_json = lmp::core::remote::track_json;
using lmp::core::Track;

namespace {

QJsonObject object(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object();
}

Track accountSong()
{
    Track track;
    track.remoteId = QStringLiteral("track-a");
    track.title = QStringLiteral("Track A");
    track.artist = QStringLiteral("Artist A");
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/track-a");
    track.artworkUrl = QStringLiteral("https://images.example.test/a.jpg");
    track.durationSeconds = 61;
    return track;
}

} // namespace

// Provider songs from JSON, and the account service's song and playlist
// request bodies.
class RemoteTrackJsonTest : public QObject {
    Q_OBJECT

private slots:
    void providerSong();
    void providerDefaults();
    void providerSongsDropped();
    void providerLists();
    void accountCompatibility();
    void accountTrackProvider();
    void playlistBodies();
};

void RemoteTrackJsonTest::providerSong()
{
    const std::optional<Track> track = track_json::trackFromProviderJson(object(R"({
      "id":"t1", "title":"Title", "artist":"Artist", "album":"Album", "provider":"source-a",
      "streamUrl":"https://media.example.test/s/1?token=abc",
      "sourceUrl":"https://music.example.test/t/1",
      "catalogId":"c1", "albumCatalogId":"a1", "artistCatalogId":"r1", "durationMs": 125400,
      "artworkUrl":"https://images.example.test/1.jpg"
    })"));
    QVERIFY(track.has_value());
    QCOMPARE(track->title, QStringLiteral("Title"));
    QCOMPARE(track->filePath, QStringLiteral("https://media.example.test/s/1?token=abc"));
    QCOMPARE(track->sourceUrl, QStringLiteral("https://music.example.test/t/1"));
    QCOMPARE(track->remoteId, QStringLiteral("t1"));
    QCOMPARE(track->catalogId, QStringLiteral("c1"));
    QCOMPARE(track->origin, QStringLiteral("source-a"));
    QCOMPARE(track->sourceKind, QStringLiteral("remote"));
    QCOMPARE(track->sourceLabel, QStringLiteral("Music API"));
    QCOMPARE(track->genre, QStringLiteral("Remote"));
    QCOMPARE(track->durationSeconds, 125.4);
    QCOMPARE(track->durationText, QStringLiteral("2:05"));
    QVERIFY(!track->artworkUrl.isEmpty());
}

void RemoteTrackJsonTest::providerDefaults()
{
    const std::optional<Track> track =
        track_json::trackFromProviderJson(object(R"({"sourceUrl":"https://music.example.test/t/2","title":7})"));
    QVERIFY(track.has_value());
    QCOMPARE(track->title, QStringLiteral("Provider track"));
    QCOMPARE(track->artist, QStringLiteral("Provider"));
    QCOMPARE(track->album, QStringLiteral("provider"));
    QCOMPARE(track->origin, QStringLiteral("provider"));
    QVERIFY(track->durationText.isEmpty());
    QVERIFY(track->filePath.isEmpty());
}

void RemoteTrackJsonTest::providerSongsDropped()
{
    const QByteArray noAddress = "{\"title\":\"No address\"}";
    const QByteArray plainHttp = "{\"streamUrl\":\"http://media.example.test/s\"}";
    const QByteArray signedSource =
        "{\"streamUrl\":\"https://media.example.test/s\",\"sourceUrl\":\"https://m.example.test/t?signature=1\"}";
    const QByteArray errorTag = "{\"sourceUrl\":\"https://music.example.test/t\",\"provider\":\"provider-error\"}";
    for (const QByteArray& body : {noAddress, plainHttp, signedSource, errorTag}) {
        QVERIFY2(!track_json::trackFromProviderJson(object(body)).has_value(), body.constData());
    }
}

void RemoteTrackJsonTest::providerLists()
{
    const QByteArray body = R"({"results":[
      {"sourceUrl":"https://music.example.test/1"}, {"title":"bad"}, 4,
      {"sourceUrl":"https://music.example.test/2"}, {"sourceUrl":"https://music.example.test/3"}]})";
    QCOMPARE(track_json::parseProviderTracks(body, 10).size(), 3);
    QCOMPARE(track_json::parseProviderTracks(body, 2).size(), 2);
    QVERIFY(track_json::parseProviderTracks("{\"results\":{}}", 10).isEmpty());
    QVERIFY(track_json::parseProviderTracks("oops", 10).isEmpty());

    const QList<Track> numbered =
        track_json::parseProviderTrackArray(QJsonDocument::fromJson(body).object().value(QLatin1StringView("results")).toArray());
    QCOMPARE(numbered.size(), 3);
    QCOMPARE(numbered.at(0).index, 1);
    QCOMPARE(numbered.at(2).index, 3);
    QCOMPARE(numbered.at(2).sourceUrl, QStringLiteral("https://music.example.test/3"));
}

void RemoteTrackJsonTest::accountCompatibility()
{
    QVERIFY(track_json::isCompatibleAccountRemoteTrack(accountSong()));
    Track noId = accountSong();
    noId.remoteId.clear();
    QVERIFY(!track_json::isCompatibleAccountRemoteTrack(noId));
    QVERIFY(track_json::isCompatibleAccountRemoteTrack(noId, false));
    Track local = accountSong();
    local.sourceKind = QStringLiteral("local");
    QVERIFY(!track_json::isCompatibleAccountRemoteTrack(local));
    Track signedAddress = accountSong();
    signedAddress.sourceUrl = QStringLiteral("https://music.example.test/t?expires=1");
    QVERIFY(!track_json::isCompatibleAccountRemoteTrack(signedAddress));
    QVERIFY(track_json::accountTrackJson(signedAddress).isEmpty());
}

void RemoteTrackJsonTest::accountTrackProvider()
{
    // An account row's origin is not a source tag.
    QJsonObject json = object(track_json::accountTrackJson(accountSong()));
    QCOMPARE(json.value(QLatin1StringView("id")).toString(), QStringLiteral("track-a"));
    QCOMPARE(json.value(QLatin1StringView("provider")).toString(), QString());
    QCOMPARE(json.value(QLatin1StringView("durationSec")).toDouble(), 61.0);

    Track tagged = accountSong();
    tagged.sourceTag = QStringLiteral("source-b");
    QCOMPARE(object(track_json::accountTrackJson(tagged)).value(QLatin1StringView("provider")).toString(),
             QStringLiteral("source-b"));
    Track fromProvider = accountSong();
    fromProvider.origin = QStringLiteral("source-c");
    QCOMPARE(object(track_json::accountTrackJson(fromProvider)).value(QLatin1StringView("provider")).toString(),
             QStringLiteral("source-c"));
}

void RemoteTrackJsonTest::playlistBodies()
{
    QCOMPARE(track_json::accountPlaylistJson(QStringLiteral("  Mix  ")), QByteArray("{\"name\":\"Mix\"}"));
    QVERIFY(track_json::accountPlaylistJson(QStringLiteral("   ")).isEmpty());
    QCOMPARE(track_json::accountPlaylistImportJson(QStringLiteral(" https://music.example.test/p/1 ")),
             QByteArray("{\"sourceUrl\":\"https://music.example.test/p/1\"}"));
    QVERIFY(track_json::accountPlaylistImportJson(QStringLiteral("ftp://x")).isEmpty());

    Track local = accountSong();
    local.sourceKind = QStringLiteral("local");
    const QJsonObject update =
        object(track_json::accountPlaylistUpdateJson(QStringLiteral(" Mix "), {accountSong(), local}));
    QCOMPARE(update.value(QLatin1StringView("name")).toString(), QStringLiteral("Mix"));
    QCOMPARE(update.value(QLatin1StringView("tracks")).toArray().size(), 1);
}

QTEST_GUILESS_MAIN(RemoteTrackJsonTest)
#include "tst_remote_track_json.moc"
