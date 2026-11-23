#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTest>

#include "core/account/AccountMusicContract.h"

namespace account = lmp::core::account;
using account::PortablePreferenceKey;

namespace {

account::PortableAccountTrack trackFixture()
{
    account::PortableAccountTrack track;
    track.id = QStringLiteral("track-a");
    track.title = QStringLiteral("Track A");
    track.artist = QStringLiteral("Artist A");
    track.album = QStringLiteral("Album A");
    track.genre = QStringLiteral("Electronic");
    track.artworkUrl = QStringLiteral("https://images.example.test/track-a.jpg");
    track.sourceKind = QStringLiteral("remote");
    track.provider = QStringLiteral("source-a");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/track-a");
    track.catalogId = QStringLiteral("catalog-track-a");
    track.albumCatalogId = QStringLiteral("catalog-album-a");
    track.artistCatalogId = QStringLiteral("catalog-artist-a");
    track.durationSec = 245.5;
    return track;
}

QJsonObject object(const QByteArray& json)
{
    return QJsonDocument::fromJson(json).object();
}

} // namespace

// The account service's song, library, history and preference payloads.
class AccountMusicContractTest : public QObject {
    Q_OBJECT

private slots:
    void portableTrackUsesCanonicalFields();
    void invalidTracksAreNotSerialized();
    void historyBatchKeepsIdentityAndWholeSeconds();
    void historyBatchKeepsDurableNestedArtwork();
    void trackRequestIsWrapped();
    void playlistReplacementCarriesTracks();
    void canonicalLibraryIsParsed();
    void libraryAliasesAndMerging();
    void notALibrary();
    void unsafeArtworkIsDropped();
    void trackAliases();
    void acknowledgedEvents();
    void preferencesAreNormalized();
    void invalidPreferencesAreDropped();
    void preferenceNamesAndValues();
};

void AccountMusicContractTest::portableTrackUsesCanonicalFields()
{
    const QJsonObject track = object(account::serializePortableTrack(trackFixture()));
    for (const char* key : {"id", "title", "artist", "album", "genre", "artworkUrl", "sourceKind", "provider",
                            "sourceUrl", "catalogId", "albumCatalogId", "artistCatalogId", "durationSec"}) {
        QVERIFY2(track.contains(QLatin1StringView(key)), key);
    }
    QCOMPARE(track.size(), 13);
    QCOMPARE(track.value(QLatin1StringView("durationSec")).toDouble(), 245.5);
}

void AccountMusicContractTest::invalidTracksAreNotSerialized()
{
    account::PortableAccountTrack local = trackFixture();
    local.sourceKind = QStringLiteral("LOCAL");
    QVERIFY(account::serializePortableTrack(local).isEmpty());
    account::PortableAccountTrack signedAddress = trackFixture();
    signedAddress.sourceUrl = QStringLiteral("https://music.example.test/t?signature=abc");
    QVERIFY(!account::isValidPortableTrack(signedAddress));
    account::PortableAccountTrack noTitle = trackFixture();
    noTitle.title.clear();
    QVERIFY(!account::isValidPortableTrack(noTitle));
    account::PortableAccountTrack negative = trackFixture();
    negative.durationSec = -4;
    QCOMPARE(object(account::serializePortableTrack(negative)).value(QLatin1StringView("durationSec")).toDouble(), 0.0);
}

void AccountMusicContractTest::historyBatchKeepsIdentityAndWholeSeconds()
{
    const account::AccountHistoryEvent event{QStringLiteral("event-a"), trackFixture(),
                                             QStringLiteral("2026-08-23T10:20:30Z"), 17.9};
    account::AccountHistoryEvent noTime = event;
    noTime.eventId = QStringLiteral("event-x");
    noTime.playedAt.clear();
    const QJsonArray events =
        object(account::serializeHistoryBatch({event, noTime})).value(QLatin1StringView("events")).toArray();
    QCOMPARE(events.size(), 1);
    const QJsonObject first = events.first().toObject();
    QCOMPARE(first.value(QLatin1StringView("eventId")).toString(), QStringLiteral("event-a"));
    QCOMPARE(first.value(QLatin1StringView("playedAt")).toString(), QStringLiteral("2026-08-23T10:20:30Z"));
    QCOMPARE(first.value(QLatin1StringView("positionSec")).toDouble(), 17.0);
    QVERIFY(!first.contains(QLatin1StringView("position")));
    const QJsonObject track = first.value(QLatin1StringView("track")).toObject();
    QCOMPARE(track.value(QLatin1StringView("provider")).toString(), QStringLiteral("source-a"));
    QCOMPARE(track.value(QLatin1StringView("artworkUrl")).toString(),
             QStringLiteral("https://images.example.test/track-a.jpg"));
}

void AccountMusicContractTest::historyBatchKeepsDurableNestedArtwork()
{
    account::PortableAccountTrack relayed = trackFixture();
    relayed.artworkUrl = QStringLiteral(
        "https://provider.example.test/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Frelayed.jpg"
        "&media_token=artwork.1.signature");
    const QJsonObject event =
        object(account::serializeHistoryBatch(
                   {{QStringLiteral("event-b"), relayed, QStringLiteral("2026-08-23T10:21:30Z"), 18.0}}))
            .value(QLatin1StringView("events"))
            .toArray()
            .first()
            .toObject();
    QCOMPARE(event.value(QLatin1StringView("track")).toObject().value(QLatin1StringView("artworkUrl")).toString(),
             QStringLiteral("https://images.example.test/relayed.jpg"));
}

void AccountMusicContractTest::trackRequestIsWrapped()
{
    const std::optional<QByteArray> wrapped = account::wrapTrackRequest(account::serializePortableTrack(trackFixture()));
    QVERIFY(wrapped.has_value());
    QCOMPARE(object(*wrapped).value(QLatin1StringView("track")).toObject().value(QLatin1StringView("id")).toString(),
             QStringLiteral("track-a"));
    QVERIFY(!account::wrapTrackRequest("{\"id\":\"x\"}").has_value());
    QVERIFY(!account::wrapTrackRequest("not json").has_value());
}

void AccountMusicContractTest::playlistReplacementCarriesTracks()
{
    const QJsonObject replacement = object(account::serializePlaylistReplacement(QStringLiteral("Playlist A"), {trackFixture()}));
    QCOMPARE(replacement.value(QLatin1StringView("name")).toString(), QStringLiteral("Playlist A"));
    QCOMPARE(replacement.value(QLatin1StringView("tracks")).toArray().size(), 1);
    QVERIFY(!replacement.contains(QLatin1StringView("trackIds")));
    const QJsonObject unnamed = object(account::serializePlaylistReplacement(QString(), {}));
    QVERIFY(!unnamed.contains(QLatin1StringView("name")));
    QVERIFY(unnamed.value(QLatin1StringView("tracks")).isArray());
}

void AccountMusicContractTest::canonicalLibraryIsParsed()
{
    const account::ParsedAccountLibrary library = account::parseAccountLibrary(R"json({
      "data": {
        "tracks": [
          {
            "track": {
              "id": "track-a", "title": "Track A", "artist": "Artist A",
              "album": "Album A", "genre": "Electronic",
              "artworkUrl": "https://images.example.test/track-a.jpg",
              "sourceKind": "remote", "provider": "source-a",
              "sourceUrl": "https://music.example.test/tracks/track-a",
              "catalogId": "catalog-track-a", "albumCatalogId": "catalog-album-a",
              "artistCatalogId": "catalog-artist-a", "durationSec": 245.5
            },
            "liked": true, "addedAt": "2026-08-23T10:00:00Z",
            "playCount": 4, "lastPositionSec": 37.5
          },
          {
            "id": "track-b", "title": "Track B", "sourceKind": "remote",
            "sourceUrl": "https://music.example.test/tracks/track-b", "durationSec": 90
          }
        ],
        "playlists": [
          { "id": "playlist-a", "name": "Playlist A", "tracks": ["track-a", "track-b"] }
        ]
      }
    })json");
    QVERIFY(library.valid);
    QCOMPARE(library.tracks.size(), 2);
    const account::AccountLibraryTrack& first = library.tracks.first();
    QCOMPARE(first.track.durationSec, 245.5);
    QVERIFY(first.liked);
    QCOMPARE(first.addedAt, QStringLiteral("2026-08-23T10:00:00Z"));
    QCOMPARE(first.lastPositionSec, 37.5);
    QCOMPARE(first.playCount, 4);
    QCOMPARE(first.track.artworkUrl, QStringLiteral("https://images.example.test/track-a.jpg"));
    QCOMPARE(first.track.provider, QStringLiteral("source-a"));
    QCOMPARE(library.tracks.at(1).track.id, QStringLiteral("track-b"));
    QCOMPARE(library.playlists.size(), 1);
    QCOMPARE(library.playlists.first().trackIds, QStringList({QStringLiteral("track-a"), QStringLiteral("track-b")}));
}

void AccountMusicContractTest::libraryAliasesAndMerging()
{
    const account::ParsedAccountLibrary library = account::parseAccountLibrary(R"json({
      "library": {
        "songs": [
          { "id": "s1", "name": "Song 1", "url": "https://music.example.test/s1", "playCount": 2,
            "playedAt": "2026-01-01T00:00:00Z", "position": 5 }
        ],
        "favorites": [
          { "trackId": "s1", "track": { "title": "Song 1", "sourceUrl": "https://music.example.test/s1" },
            "imageUrl": "https://images.example.test/s1.jpg" }
        ],
        "recentlyPlayed": [
          { "track": { "id": "s1", "title": "Song 1", "sourceUrl": "https://music.example.test/s1" },
            "stats": { "playCount": 7, "lastPlayedAt": "2026-02-01T00:00:00Z", "lastPosition": 12,
                       "firstPlayedAtUtc": "2025-12-01T00:00:00Z" } },
          { "track": { "id": "s2", "title": "Song 2", "sourceUrl": "https://music.example.test/s2",
                       "durationMs": 61000 } }
        ],
        "playlists": [
          { "playlistId": "p1", "title": "Mix", "updatedAt": "2026-03-01", "items": [
              "s2", "s2", "missing",
              { "id": "s3", "title": "Song 3", "sourceUrl": "https://music.example.test/s3" } ] },
          { "id": "p1", "name": "Duplicate" },
          { "name": "No id" }
        ]
      }
    })json");
    QVERIFY(library.valid);
    QCOMPARE(library.tracks.size(), 3);
    const account::AccountLibraryTrack& s1 = library.tracks.at(0);
    QVERIFY(s1.liked);
    QCOMPARE(s1.playCount, 7);
    QCOMPARE(s1.lastPlayedAt, QStringLiteral("2026-02-01T00:00:00Z"));
    QCOMPARE(s1.lastPositionSec, 12.0);
    QCOMPARE(s1.track.artworkUrl, QStringLiteral("https://images.example.test/s1.jpg"));
    QCOMPARE(library.tracks.at(1).track.durationSec, 61.0);
    QCOMPARE(library.tracks.at(2).track.id, QStringLiteral("s3"));
    QCOMPARE(library.playlists.size(), 1);
    QCOMPARE(library.playlists.first().name, QStringLiteral("Mix"));
    QCOMPARE(library.playlists.first().trackIds, QStringList({QStringLiteral("s2"), QStringLiteral("s3")}));
}

void AccountMusicContractTest::notALibrary()
{
    QVERIFY(!account::parseAccountLibrary("{\"profile\":{}}").valid);
    QVERIFY(!account::parseAccountLibrary("[]").valid);
    QVERIFY(!account::parseAccountLibrary("garbage").valid);
    const account::ParsedAccountLibrary empty = account::parseAccountLibrary("{\"playlists\":[]}");
    QVERIFY(empty.valid);
    QVERIFY(empty.tracks.isEmpty());
}

void AccountMusicContractTest::unsafeArtworkIsDropped()
{
    const std::optional<account::PortableAccountTrack> track = account::parsePortableTrack(R"json({
      "id":"track-a", "title":"Track A", "sourceKind":"remote",
      "sourceUrl":"https://music.example.test/tracks/track-a",
      "artworkUrl":"javascript:alert(1)"
    })json");
    QVERIFY(track.has_value());
    QVERIFY(track->artworkUrl.isEmpty());
}

void AccountMusicContractTest::trackAliases()
{
    const std::optional<account::PortableAccountTrack> track = account::parsePortableTrack(R"json({
      "remoteId":"r1", "name":"Name", "artistName":"Artist", "albumName":"Album",
      "url":"https://music.example.test/r1", "sourceId":"c1", "albumId":"a1", "artistId":"ar1",
      "durationSeconds": 30
    })json");
    QVERIFY(track.has_value());
    QCOMPARE(track->id, QStringLiteral("r1"));
    QCOMPARE(track->title, QStringLiteral("Name"));
    QCOMPARE(track->artist, QStringLiteral("Artist"));
    QCOMPARE(track->album, QStringLiteral("Album"));
    QCOMPARE(track->catalogId, QStringLiteral("c1"));
    QCOMPARE(track->albumCatalogId, QStringLiteral("a1"));
    QCOMPARE(track->artistCatalogId, QStringLiteral("ar1"));
    QCOMPARE(track->durationSec, 30.0);
    QCOMPARE(track->sourceKind, QStringLiteral("remote"));
    const QByteArray fileAddress = "{\"id\":\"x\",\"title\":\"t\",\"sourceUrl\":\"file:///c:/a.mp3\"}";
    QVERIFY(!account::parsePortableTrack(fileAddress).has_value());
}

void AccountMusicContractTest::acknowledgedEvents()
{
    const QStringList acknowledged = account::parseAcknowledgedHistoryEvents(
        R"({"accepted":["e1","e9",3,"e1"],"duplicates":["e2","e1"]})",
        {QStringLiteral("e1"), QStringLiteral("e2"), QStringLiteral("e3")});
    QCOMPARE(acknowledged, QStringList({QStringLiteral("e1"), QStringLiteral("e2")}));
    QVERIFY(account::parseAcknowledgedHistoryEvents("nope", {QStringLiteral("e1")}).isEmpty());
}

void AccountMusicContractTest::preferencesAreNormalized()
{
    const account::PortablePreferences preferences = account::parsePortablePreferences(R"({
      "data": { "preferences": {
        "theme":"dark", "accent":"#12abEF", "gaplessPlayback":true,
        "dataSaver":false, "storefront":"IN"
      }}
    })");
    QCOMPARE(preferences.theme, std::optional<QString>(QStringLiteral("dark")));
    QCOMPARE(preferences.accent, std::optional<QString>(QStringLiteral("#12ABEF")));
    QCOMPARE(preferences.gaplessPlayback, std::optional<bool>(true));
    QCOMPARE(preferences.dataSaver, std::optional<bool>(false));
    QCOMPARE(preferences.storefront, std::optional<QString>(QStringLiteral("in")));

    const QJsonObject written = object(account::serializePortablePreferences(preferences));
    QCOMPARE(written.value(QLatin1StringView("accent")).toString(), QStringLiteral("#12ABEF"));
    QCOMPARE(written.value(QLatin1StringView("gaplessPlayback")).toBool(), true);
    QCOMPARE(written.size(), 5);
    QCOMPARE(account::parsePortablePreferences(account::serializePortablePreferences(preferences)), preferences);
}

void AccountMusicContractTest::invalidPreferencesAreDropped()
{
    const account::PortablePreferences invalid = account::parsePortablePreferences(
        R"({"theme":"sepia","accent":"red","gaplessPlayback":"yes","storefront":"india"})");
    QVERIFY(!invalid.theme && !invalid.accent && !invalid.gaplessPlayback && !invalid.storefront);
    QCOMPARE(account::serializePortablePreferences({}), QByteArray("{}"));
}

void AccountMusicContractTest::preferenceNamesAndValues()
{
    QCOMPARE(account::portablePreferenceName(PortablePreferenceKey::GaplessPlayback), QStringLiteral("gaplessPlayback"));
    QCOMPARE(account::parsePortablePreferenceName(QStringLiteral("dataSaver")),
             std::optional<PortablePreferenceKey>(PortablePreferenceKey::DataSaver));
    QVERIFY(!account::parsePortablePreferenceName(QStringLiteral("Theme")).has_value());

    QCOMPARE(account::validatePortablePreferenceValue(PortablePreferenceKey::Theme, QStringLiteral("\"LIGHT\"")),
             std::optional<QString>(QStringLiteral("\"light\"")));
    QCOMPARE(account::validatePortablePreferenceValue(PortablePreferenceKey::DataSaver, QStringLiteral("true")),
             std::optional<QString>(QStringLiteral("true")));
    QVERIFY(!account::validatePortablePreferenceValue(PortablePreferenceKey::DataSaver, QStringLiteral("\"true\"")));
    QVERIFY(!account::validatePortablePreferenceValue(PortablePreferenceKey::Accent, QStringLiteral("#123456")));
    QVERIFY(!account::validatePortablePreferenceValue(PortablePreferenceKey::Storefront, QStringLiteral("\"u1\"")));

    account::PortablePreferences preferences;
    preferences.theme = QStringLiteral("system");
    preferences.dataSaver = true;
    const account::PortablePreferenceValues values = account::portablePreferenceValues(preferences);
    QCOMPARE(values.value(PortablePreferenceKey::Theme), QStringLiteral("\"system\""));
    QCOMPARE(values.value(PortablePreferenceKey::DataSaver), QStringLiteral("true"));
    QCOMPARE(values.size(), 2);
    QCOMPARE(account::portablePreferencesFromValues(values), preferences);

    account::PortablePreferenceValues mixed = values;
    mixed.insert(PortablePreferenceKey::Accent, QStringLiteral("\"blue\""));
    mixed.insert(PortablePreferenceKey::Storefront, QStringLiteral("\"GB\""));
    const account::PortablePreferences read = account::portablePreferencesFromValues(mixed);
    QVERIFY(!read.accent);
    QCOMPARE(read.storefront, std::optional<QString>(QStringLiteral("gb")));
}

QTEST_GUILESS_MAIN(AccountMusicContractTest)
#include "tst_account_music_contract.moc"
