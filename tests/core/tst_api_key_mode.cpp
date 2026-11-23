// API key mode's screens without a provider: the follow list per provider,
// the provider mixes, and importing a link (view models and services over
// fake answers, as P09-04 allows when no provider runs).

#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "CatalogFixtures.h"
#include "ModeServiceFixture.h"
#include "core/catalog/CatalogClient.h"
#include "core/db/CollectionRepository.h"
#include "core/db/FollowedArtistRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/ArtistFollowService.h"
#include "core/library/HomeMixService.h"
#include "core/library/LinkImportService.h"
#include "core/library/UserDataOperationGate.h"
#include "core/remote/RemoteMusicService.h"

using lmp::core::HomeMix;
using lmp::core::HomeMixService;
using lmp::core::Result;
using lmp::core::Track;
using lmp::test::json;

namespace {

Track remoteSong(const QString& id)
{
    Track track;
    track.title = QStringLiteral("Song ") + id;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceTag = QStringLiteral("tag-b");
    track.remoteId = QStringLiteral("tag-b:") + id;
    track.sourceUrl = QStringLiteral("https://music.example.test/") + id;
    return track;
}

} // namespace

class ApiKeyModeTest : public QObject {
    Q_OBJECT

private slots:
    void followsAreKeptPerProvider();
    void providerMixesAskTheEarlierSearches();
    void providerSongsJoinAMixOnce();
    void importAnswerIsRead();
    void importedAlbumAndPlaylistAreStored();
    void accountImportIsReadAsTheReferenceDoes();
};

void ApiKeyModeTest::followsAreKeptPerProvider()
{
    lmp::test::ModeServiceFixture modes;
    modes.storeProvider();
    (void)modes.modes.start();
    QVERIFY(modes.modes.apiKeySelected());
    lmp::core::db::FollowedArtistRepository followed(modes.library.database());
    lmp::core::UserDataOperationGate gate;
    lmp::core::ArtistFollowService follows({modes.session.client, modes.session.service, modes.modes, followed, gate});
    lmp::core::FollowableArtist artist;
    artist.identity.name = QStringLiteral("Mira Sol");
    artist.identity.provider = QStringLiteral("tag-b");
    QVERIFY(follows.canFollow(artist));
    QFuture<Result<bool>> done = follows.setFollowed(artist, true);
    QTRY_VERIFY(done.isFinished());
    QVERIFY(done.result().isOk());
    QVERIFY(follows.isFollowed(artist));
    const QString scope = QStringLiteral("api-key|")
        + lmp::core::remote::apiKeyCachePartition(lmp::test::kProviderBase, QStringLiteral("key-a"));
    QVERIFY(followed.isArtistFollowed(scope, lmp::core::artistIdentityKey(artist.identity)));
    QVERIFY(!followed.isArtistFollowed(QString(lmp::core::ArtistFollowService::LocalScope),
                                       lmp::core::artistIdentityKey(artist.identity)));
    // Local mode keeps its own list.
    QVERIFY(modes.modes.setMode(lmp::core::mode::SourceMode::LocalOnly));
    QVERIFY(!follows.isFollowed(artist));
}

void ApiKeyModeTest::providerMixesAskTheEarlierSearches()
{
    QList<HomeMix> mixes;
    HomeMix genreMix;
    genreMix.id = QStringLiteral("daily2");
    genreMix.genre = QStringLiteral("Jazz");
    mixes.append(genreMix);
    const QList<HomeMixService::FillRequest> requests =
        HomeMixService::fillRequests(mixes, {QStringLiteral("A1"), QStringLiteral("A2")});
    QCOMPARE(requests.size(), 7);
    QCOMPARE(requests.at(0).query, QStringLiteral("A1"));
    QCOMPARE(requests.at(1).query, QStringLiteral("Jazz songs"));
    QCOMPARE(requests.at(2).query, QStringLiteral("rock alternative songs"));
    QCOMPARE(requests.at(5).id, QStringLiteral("repeat"));
    QCOMPARE(requests.at(5).query, QStringLiteral("A1"));
    QCOMPARE(requests.at(6).query, QStringLiteral("A1 new songs"));
    QVERIFY(requests.at(6).prepend);
    QCOMPARE(HomeMixService::fillRequests({}, {}).at(6).query, QStringLiteral("new music"));
}

void ApiKeyModeTest::providerSongsJoinAMixOnce()
{
    QList<Track> mix;
    for (int index = 0; index < 10; ++index) {
        mix.append(remoteSong(QStringLiteral("m%1").arg(index)));
    }
    Track unplayable = remoteSong(QStringLiteral("x"));
    unplayable.sourceUrl.clear();
    const QList<Track> found{remoteSong(QStringLiteral("m0")), remoteSong(QStringLiteral("n1")), unplayable,
                             remoteSong(QStringLiteral("n2")), remoteSong(QStringLiteral("n3"))};
    const QList<Track> appended = HomeMixService::joinFill(mix, found, false);
    QCOMPARE(appended.size(), HomeMixService::FilledMixSize);
    QCOMPARE(appended.at(10).title, QStringLiteral("Song n1"));
    QCOMPARE(appended.at(11).title, QStringLiteral("Song n2"));
    const QList<Track> prepended = HomeMixService::joinFill(mix, found, true);
    QCOMPARE(prepended.first().title, QStringLiteral("Song m0"));
    QCOMPARE(prepended.at(1).title, QStringLiteral("Song n1"));
}

void ApiKeyModeTest::importAnswerIsRead()
{
    const std::optional<lmp::core::ImportedCollection> playlist = lmp::core::parseImportedCollection(
        json("{'album':{'title':'Mix','provider':'tag-b','collectionKind':'Playlist'},"
             "'tracks':[{'id':'tag-b:1','provider':'tag-b','title':'One','sourceUrl':'https://music.example.test/1'}]}"),
        QStringLiteral("https://music.example.test/list"));
    QVERIFY(playlist.has_value());
    QCOMPARE(playlist->kind, QStringLiteral("playlist"));
    QCOMPARE(playlist->sourceUrl, QStringLiteral("https://music.example.test/list"));
    QCOMPARE(playlist->artist, QStringLiteral("Music API"));
    const std::optional<lmp::core::ImportedCollection> album = lmp::core::parseImportedCollection(
        json("{'tracks':[{'id':'tag-b:1','provider':'tag-b','title':'One','sourceUrl':'https://music.example.test/1'}]}"),
        QStringLiteral("https://music.example.test/a"));
    QVERIFY(album.has_value());
    QCOMPARE(album->kind, QStringLiteral("album"));
    QCOMPARE(album->title, QStringLiteral("Imported Album"));
    QCOMPARE(album->provider, QStringLiteral("remote"));
    QVERIFY(!lmp::core::parseImportedCollection(json("{'tracks':[]}"), QString()).has_value());
}

void ApiKeyModeTest::importedAlbumAndPlaylistAreStored()
{
    lmp::test::ModeServiceFixture modes;
    (void)modes.modes.start();
    lmp::core::db::Database& database = modes.library.database();
    lmp::core::db::TrackRepository tracks(database);
    lmp::core::db::CollectionRepository collections(database);
    lmp::core::db::GroupQueries groups(database);
    lmp::core::catalog::CatalogClient catalog(modes.session.client);
    lmp::core::remote::RemoteMusicService remoteMusic(
        {modes.modes, modes.session.service, modes.session.client, catalog, modes.provider});
    lmp::core::UserDataOperationGate gate;
    lmp::core::LinkImportService importer({remoteMusic, tracks, collections, gate});

    lmp::core::ImportedCollection album;
    album.title = QStringLiteral("Night Album");
    album.artist = QStringLiteral("Artist");
    album.provider = QStringLiteral("Tag-B");
    album.sourceUrl = QStringLiteral("https://music.example.test/album/1");
    album.kind = QStringLiteral("album");
    album.tracks = {remoteSong(QStringLiteral("1")), remoteSong(QStringLiteral("2"))};
    const Result<lmp::core::ImportOutcome> stored = importer.store(album);
    QVERIFY(stored.isOk());
    QCOMPARE(stored.value().kind, QStringLiteral("album-collection"));
    QCOMPARE(stored.value().key, QStringLiteral("import|tag-b|https://music.example.test/album/1"));
    QCOMPARE(stored.value().songCount, 2);
    // Importing again replaces the songs, under the same key.
    album.tracks = {remoteSong(QStringLiteral("3"))};
    QCOMPARE(importer.store(album).value().key, stored.value().key);

    lmp::core::ImportedCollection playlist = album;
    playlist.kind = QStringLiteral("playlist");
    const Result<lmp::core::ImportOutcome> listed = importer.store(playlist);
    QVERIFY(listed.isOk());
    QCOMPARE(listed.value().key, QStringLiteral("import-playlist|tag-b|https://music.example.test/album/1"));
    bool found = false;
    for (const Track& entry : groups.loadPlaylists()) {
        found = found || entry.title == QStringLiteral("Night Album");
    }
    QVERIFY(found);

    // Refused while user data is being cleaned up; outside a provider mode the
    // link is not even asked for.
    QVERIFY(gate.closeAdmissions());
    QCOMPARE(importer.store(album).failure().kind, lmp::core::FailureKind::Cancelled);
    gate.reopen();
    QFuture<Result<lmp::core::ImportOutcome>> refused = importer.importLink(QStringLiteral("https://music.example.test/a"));
    QTRY_VERIFY(refused.isFinished());
    QCOMPARE(refused.result().failure().kind, lmp::core::FailureKind::NotAvailable);
}

void ApiKeyModeTest::accountImportIsReadAsTheReferenceDoes()
{
    const std::optional<lmp::core::ImportedCollection> named = lmp::core::parseAccountImport(
        json("{'album':{'title':'Road Trip','artist':'Various'},'results':[{'id':'tag-b:1','provider':'tag-b',"
             "'title':'One','album':'A','sourceUrl':'https://music.example.test/1'}]}"),
        QStringLiteral("https://music.example.test/list"));
    QVERIFY(named.has_value());
    QCOMPARE(named->title, QStringLiteral("Road Trip"));
    QCOMPARE(named->artist, QStringLiteral("Various"));
    QCOMPARE(named->kind, QStringLiteral("playlist"));
    // Without a title, the songs' most common album names it.
    const std::optional<lmp::core::ImportedCollection> byAlbum = lmp::core::parseAccountImport(
        json("{'tracks':[{'id':'tag-b:1','provider':'tag-b','title':'One','album':'Blue','sourceUrl':'https://music.example.test/1'},"
             "{'id':'tag-b:2','provider':'tag-b','title':'Two','album':'Blue','sourceUrl':'https://music.example.test/2'},"
             "{'id':'tag-b:3','provider':'tag-b','title':'Three','album':'Red','sourceUrl':'https://music.example.test/3'}]}"),
        QStringLiteral("https://music.example.test/list"));
    QVERIFY(byAlbum.has_value());
    QCOMPARE(byAlbum->title, QStringLiteral("Blue"));
    QVERIFY(!lmp::core::parseAccountImport(json("{'results':[]}"), QString()).has_value());
}

QTEST_GUILESS_MAIN(ApiKeyModeTest)
#include "tst_api_key_mode.moc"
