#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/AccountRecords.h"
#include "core/db/AccountRepository.h"
#include "core/db/LibraryQueries.h"
#include "core/model/Track.h"

using lmp::core::FailureKind;
using lmp::core::Track;
using lmp::core::db::AccountLibrarySnapshot;
using lmp::core::db::AccountPlaylistSnapshot;
using lmp::core::db::AccountRepository;
using lmp::core::db::AccountTrackSnapshot;
using lmp::core::db::LibraryQueries;
using lmp::core::db::TrackQuery;

namespace {

Track accountSong(const QString& remoteId, const QString& title, double sortKey = 0)
{
    Track track;
    track.remoteId = remoteId;
    track.title = title;
    track.artist = QStringLiteral("Artist ") + title;
    track.album = QStringLiteral("Album One");
    track.genre = QStringLiteral("Ambient");
    track.durationSeconds = 180;
    track.durationText = QStringLiteral("3:00");
    track.sourceUrl = QStringLiteral("https://music.example.test/tracks/") + remoteId;
    track.artworkUrl = QStringLiteral("https://images.example.test/") + remoteId + QStringLiteral(".jpg");
    track.dateAddedSortKey = sortKey;
    track.sourceTag = QStringLiteral("source-a");
    track.catalogId = QStringLiteral("catalog-") + remoteId;
    track.albumCatalogId = QStringLiteral("album-1");
    track.artistCatalogId = QStringLiteral("artist-1");
    return track;
}

AccountTrackSnapshot entry(const Track& track, qint64 playCount = 0)
{
    AccountTrackSnapshot snapshot;
    snapshot.track = track;
    snapshot.playCount = playCount;
    snapshot.firstPlayedAtUtc = QStringLiteral("2026-08-01T00:00:00Z");
    snapshot.lastPlayedAtUtc = QStringLiteral("2026-08-02T00:00:00Z");
    snapshot.lastPositionSeconds = 12.5;
    return snapshot;
}

AccountLibrarySnapshot librarySnapshot(const QString& accountId)
{
    AccountLibrarySnapshot snapshot;
    snapshot.profile.accountId = accountId;
    snapshot.profile.displayName = QStringLiteral("Listener");
    snapshot.profile.username = QStringLiteral("listener");
    snapshot.profile.avatarUrl = QStringLiteral("https://images.example.test/me.png");
    snapshot.profile.planLabel = QStringLiteral("Plus");
    snapshot.profile.updatedAtUtc = QStringLiteral("2026-08-20T10:00:00Z");
    snapshot.synchronizedAtUtc = QStringLiteral("2026-08-21T10:00:00Z");
    snapshot.tracks = {entry(accountSong(QStringLiteral("r1"), QStringLiteral("Older"), 100), 3),
                       entry(accountSong(QStringLiteral("r2"), QStringLiteral("Newer"), 200)),
                       entry(accountSong(QStringLiteral("r1"), QStringLiteral("Repeat"), 300)),
                       entry(accountSong(QString(), QStringLiteral("No id"), 400))};
    AccountPlaylistSnapshot playlist;
    playlist.playlistId = QStringLiteral("pl-1");
    playlist.name = QStringLiteral("Morning");
    playlist.description = QStringLiteral("Quiet songs");
    playlist.sourceUrl = QStringLiteral("https://music.example.test/playlists/1");
    playlist.updatedAtUtc = QStringLiteral("2026-08-19T10:00:00Z");
    playlist.remoteTrackIds = {QStringLiteral("r2"), QStringLiteral("missing"), QStringLiteral("r1")};
    AccountPlaylistSnapshot repeated = playlist;
    repeated.name = QStringLiteral("Repeated id");
    AccountPlaylistSnapshot unnamed = playlist;
    unnamed.playlistId.clear();
    snapshot.playlists = {playlist, repeated, unnamed};
    return snapshot;
}

} // namespace

class AccountRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void remoteContextValidatesModeAndAccount();
    void replaceRefusesWhenTheAccountIsNotActive();
    void replaceStoresTheSnapshotAndLoadsItBack();
    void replaceCleansAddressesBeforeStoring();
    void replaceSwapsOutTheWholePreviousLibrary();
    void loadWithoutSyncStateUsesNewestSongUpdate();
    void playlistTracksReadInOrderWithNegatedIds();
    void libraryQueriesPagesAnAccountPlaylist();
    void ensureTrackKeepsStoredValuesForEmptyFields();
    void recordPlaybackCountsPlaysAndClampsPosition();
    void likeUpdateTouchesOnlyTheCachedSong();
};

void AccountRepositoryTest::remoteContextValidatesModeAndAccount()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());

    QCOMPARE(accounts.activeAccountId(), QString());
    const auto unknown = accounts.setRemoteLibraryContext(QStringLiteral("account"), QStringLiteral("owner-a"));
    QVERIFY(!unknown);
    QCOMPARE(unknown.failure().kind, FailureKind::NotFound);
    const auto noOwner = accounts.setRemoteLibraryContext(QStringLiteral("Account"));
    QVERIFY(!noOwner);
    QCOMPARE(noOwner.failure().kind, FailureKind::NotFound);

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QCOMPARE(accounts.activeAccountId(), QStringLiteral("owner-a"));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles WHERE AccountId='owner-a';"), 1);

    // Other modes never store an account id.
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("ApiKey"), QStringLiteral("owner-a")));
    QCOMPARE(accounts.activeAccountId(), QString());
    QCOMPARE(library.text("SELECT RemoteMode || '|' || AccountId FROM ActiveAccountContext;"), QStringLiteral("ApiKey|"));
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("LocalOnly")));
    QCOMPARE(library.text("SELECT RemoteMode FROM ActiveAccountContext;"), QStringLiteral("LocalOnly"));
}

void AccountRepositoryTest::replaceRefusesWhenTheAccountIsNotActive()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    const AccountLibrarySnapshot snapshot = librarySnapshot(QStringLiteral("owner-a"));

    const auto inactive = accounts.replaceAccountLibrary(QStringLiteral("owner-a"), snapshot);
    QVERIFY(!inactive);
    QCOMPARE(inactive.failure().kind, FailureKind::NotFound);

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-b")));
    QVERIFY(!accounts.replaceAccountLibrary(QStringLiteral("owner-a"), snapshot));
    QVERIFY(!accounts.replaceAccountLibrary(QStringLiteral("owner-b"), snapshot));
    QVERIFY(!accounts.replaceAccountLibrary(QString(), snapshot));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountTracks;"), 0);
}

void AccountRepositoryTest::replaceStoresTheSnapshotAndLoadsItBack()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    lmp::core::db::AccountSyncState earlier;
    earlier.accountId = QStringLiteral("owner-a");
    earlier.lastSafeErrorCode = QStringLiteral("network");
    earlier.legacyHistoryImportState = QStringLiteral("accepted");
    QVERIFY(accounts.saveAccountSyncState(earlier));

    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), librarySnapshot(QStringLiteral("owner-a"))));

    const auto loaded = accounts.loadAccountLibrary(QStringLiteral("owner-a"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->profile.displayName, QStringLiteral("Listener"));
    QCOMPARE(loaded->profile.username, QStringLiteral("listener"));
    QCOMPARE(loaded->profile.avatarUrl, QStringLiteral("https://images.example.test/me.png"));
    QCOMPARE(loaded->profile.planLabel, QStringLiteral("Plus"));
    QCOMPARE(loaded->profile.updatedAtUtc, QStringLiteral("2026-08-20T10:00:00Z"));
    QCOMPARE(loaded->synchronizedAtUtc, QStringLiteral("2026-08-21T10:00:00Z"));

    // The first song with an id wins; songs without one are skipped.
    QCOMPARE(loaded->tracks.size(), 2);
    QCOMPARE(loaded->tracks.at(0).track.title, QStringLiteral("Newer"));
    const AccountTrackSnapshot& older = loaded->tracks.at(1);
    QCOMPARE(older.track.title, QStringLiteral("Older"));
    QCOMPARE(older.playCount, 3);
    QCOMPARE(older.firstPlayedAtUtc, QStringLiteral("2026-08-01T00:00:00Z"));
    QCOMPARE(older.lastPlayedAtUtc, QStringLiteral("2026-08-02T00:00:00Z"));
    QCOMPARE(older.lastPositionSeconds, 12.5);
    QCOMPARE(older.track.origin, QStringLiteral("account"));
    QCOMPARE(older.track.sourceLabel, QStringLiteral("Account"));
    QCOMPARE(older.track.sourceTag, QStringLiteral("source-a"));
    QVERIFY(older.track.rowId < 0);

    // Repeated and unnamed playlists are skipped; entries name stored songs only.
    QCOMPARE(loaded->playlists.size(), 1);
    QCOMPARE(loaded->playlists.at(0).name, QStringLiteral("Morning"));
    QCOMPARE(loaded->playlists.at(0).description, QStringLiteral("Quiet songs"));
    QCOMPARE(loaded->playlists.at(0).remoteTrackIds, (QStringList{QStringLiteral("r2"), QStringLiteral("r1")}));
    QCOMPARE(library.text("SELECT group_concat(TrackOrder) FROM (SELECT TrackOrder FROM AccountPlaylistTracks ORDER BY TrackOrder);"), QStringLiteral("0,1"));

    // The import decision survives; the last error does not.
    const auto state = accounts.loadAccountSyncState(QStringLiteral("owner-a"));
    QCOMPARE(state.lastSuccessfulSyncUtc, QStringLiteral("2026-08-21T10:00:00Z"));
    QCOMPARE(state.legacyHistoryImportState, QStringLiteral("accepted"));
    QCOMPARE(state.lastSafeErrorCode, QString());

    QVERIFY(!accounts.loadAccountLibrary(QStringLiteral("owner-unknown")).has_value());
    QVERIFY(!accounts.loadAccountLibrary(QString()).has_value());
}

void AccountRepositoryTest::replaceCleansAddressesBeforeStoring()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));

    AccountLibrarySnapshot snapshot = librarySnapshot(QStringLiteral("owner-a"));
    snapshot.profile.avatarUrl = QStringLiteral("https://images.example.test/me.png?token=secret");
    Track& signedSong = snapshot.tracks[0].track;
    signedSong.sourceUrl = QStringLiteral("https://music.example.test/a?signature=secret");
    signedSong.artworkUrl = QStringLiteral(
        "https://relay.example.test/v1/artwork?url=https%3A%2F%2Fimages.example.test%2Fcover.jpg&media_token=t");
    snapshot.playlists[0].sourceUrl = QStringLiteral("https://music.example.test/p?access_token=secret");
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), snapshot));

    QCOMPARE(library.text("SELECT AvatarUrl FROM AccountProfiles;"), QString());
    QCOMPARE(library.text("SELECT SourceUrl FROM AccountTracks WHERE RemoteId='r1';"), QString());
    QCOMPARE(library.text("SELECT ArtworkUrl FROM AccountTracks WHERE RemoteId='r1';"),
             QStringLiteral("https://images.example.test/cover.jpg"));
    QCOMPARE(library.text("SELECT SourceUrl FROM AccountPlaylists;"), QString());

    // An inline picture is kept as it is.
    const QString inlinePicture = QStringLiteral("data:image/png;base64,AAAA");
    snapshot.profile.avatarUrl = inlinePicture;
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), snapshot));
    QCOMPARE(library.text("SELECT AvatarUrl FROM AccountProfiles;"), inlinePicture);
}

void AccountRepositoryTest::replaceSwapsOutTheWholePreviousLibrary()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), librarySnapshot(QStringLiteral("owner-a"))));

    AccountLibrarySnapshot smaller;
    smaller.profile.accountId = QStringLiteral("owner-a");
    smaller.tracks = {entry(accountSong(QStringLiteral("r9"), QStringLiteral("Only")))};
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), smaller));
    QCOMPARE(library.text("SELECT group_concat(RemoteId) FROM AccountTracks;"), QStringLiteral("r9"));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountPlaylists;"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountPlaylistTracks;"), 0);
    QCOMPARE(library.text("SELECT DisplayName FROM AccountProfiles;"), QString());
}

void AccountRepositoryTest::loadWithoutSyncStateUsesNewestSongUpdate()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-a"), accountSong(QStringLiteral("r1"), QStringLiteral("A"))));
    QVERIFY(library.connection()
                .exec("UPDATE AccountTracks SET UpdatedAtUtc='2026-09-01 08:00:00' WHERE RemoteId='r1';")
                .isOk());
    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-a"), accountSong(QStringLiteral("r2"), QStringLiteral("B"))));
    QVERIFY(library.connection()
                .exec("UPDATE AccountTracks SET UpdatedAtUtc='2026-09-03 08:00:00' WHERE RemoteId='r2';")
                .isOk());

    const auto loaded = accounts.loadAccountLibrary(QStringLiteral("owner-a"));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->synchronizedAtUtc, QStringLiteral("2026-09-03 08:00:00"));
}

void AccountRepositoryTest::playlistTracksReadInOrderWithNegatedIds()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), librarySnapshot(QStringLiteral("owner-a"))));

    const QList<Track> tracks = accounts.loadAccountPlaylistTracks(QStringLiteral("owner-a"), QStringLiteral("pl-1"));
    QCOMPARE(tracks.size(), 2);
    QCOMPARE(tracks.at(0).remoteId, QStringLiteral("r2"));
    QCOMPARE(tracks.at(1).remoteId, QStringLiteral("r1"));
    QCOMPARE(tracks.at(0).index, 1);
    QCOMPARE(tracks.at(1).index, 2);
    const Track& song = tracks.at(1);
    QCOMPARE(song.rowId, -library.number("SELECT rowid FROM AccountTracks WHERE RemoteId='r1';"));
    QCOMPARE(song.title, QStringLiteral("Older"));
    QCOMPARE(song.durationText, QStringLiteral("3:00"));
    QCOMPARE(song.catalogId, QStringLiteral("catalog-r1"));
    QCOMPARE(song.albumCatalogId, QStringLiteral("album-1"));
    QCOMPARE(song.artistCatalogId, QStringLiteral("artist-1"));
    QCOMPARE(song.sourceKind, QStringLiteral("remote"));
    QCOMPARE(song.filePath, QString());

    QVERIFY(accounts.loadAccountPlaylistTracks(QStringLiteral("owner-b"), QStringLiteral("pl-1")).isEmpty());
    QVERIFY(accounts.loadAccountPlaylistTracks(QStringLiteral("owner-a"), QString()).isEmpty());
}

void AccountRepositoryTest::libraryQueriesPagesAnAccountPlaylist()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QVERIFY(accounts.replaceAccountLibrary(QStringLiteral("owner-a"), librarySnapshot(QStringLiteral("owner-a"))));

    LibraryQueries queries(library.database(), &accounts);
    TrackQuery query;
    query.groupKind = QStringLiteral("playlist");
    query.groupKey = QStringLiteral("account-playlist|pl-1");
    query.accountOwnerId = QStringLiteral("owner-a");
    query.sort = QStringLiteral("Title");
    query.limit = 1;
    const auto page = queries.loadTrackPage(query);
    QCOMPARE(page.totalCount, 2);
    QCOMPARE(page.totalSeconds, 360.0);
    QCOMPARE(page.tracks.size(), 1);
    QCOMPARE(page.tracks.at(0).title, QStringLiteral("Newer"));

    query.offset = 1;
    QCOMPARE(queries.loadTrackPage(query).tracks.at(0).title, QStringLiteral("Older"));

    query.accountOwnerId = QStringLiteral("owner-b");
    QCOMPARE(queries.loadTrackPage(query).totalCount, 0);
}

void AccountRepositoryTest::ensureTrackKeepsStoredValuesForEmptyFields()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());

    Track unsafe = accountSong(QStringLiteral("r1"), QStringLiteral("A"));
    unsafe.sourceUrl = QStringLiteral("https://music.example.test/a?token=secret");
    const auto refused = accounts.ensureAccountTrack(QStringLiteral("owner-a"), unsafe);
    QVERIFY(!refused);
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
    QVERIFY(!accounts.ensureAccountTrack(QString(), accountSong(QStringLiteral("r1"), QStringLiteral("A"))));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles;"), 0);

    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-a"), accountSong(QStringLiteral("r1"), QStringLiteral("A"))));
    Track sparse;
    sparse.remoteId = QStringLiteral("r1");
    sparse.sourceUrl = QStringLiteral("https://music.example.test/moved");
    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-a"), sparse));
    QCOMPARE(library.text("SELECT Title || '|' || Artist || '|' || DurationSeconds || '|' || RemoteCatalogId || '|' || SourceUrl "
                          "FROM AccountTracks;"),
             QStringLiteral("A|Artist A|180.0|catalog-r1|https://music.example.test/moved"));
}

void AccountRepositoryTest::recordPlaybackCountsPlaysAndClampsPosition()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    const Track song = accountSong(QStringLiteral("r1"), QStringLiteral("A"));

    QVERIFY(accounts.recordAccountPlayback(QStringLiteral("owner-a"), song, 42.0));
    const QString firstPlayed = library.text("SELECT FirstPlayedAtUtc FROM AccountTracks;");
    QVERIFY(!firstPlayed.isEmpty());
    QVERIFY(library.connection().exec("UPDATE AccountTracks SET FirstPlayedAtUtc='2026-01-01 00:00:00';").isOk());
    QVERIFY(accounts.recordAccountPlayback(QStringLiteral("owner-a"), song, -5.0));
    QCOMPARE(library.text("SELECT PlayCount || '|' || LastPositionSeconds || '|' || FirstPlayedAtUtc FROM AccountTracks;"),
             QStringLiteral("2|0.0|2026-01-01 00:00:00"));
    QVERIFY(!library.text("SELECT LastPlayedAtUtc FROM AccountTracks;").isEmpty());

    Track unsafe = song;
    unsafe.sourceUrl = QStringLiteral("ftp://music.example.test/a");
    QVERIFY(!accounts.recordAccountPlayback(QStringLiteral("owner-a"), unsafe, 1.0));
    QCOMPARE(library.number("SELECT PlayCount FROM AccountTracks;"), 2);
    // The failed transaction was rolled back, so another one can start.
    QVERIFY(library.connection().exec("BEGIN IMMEDIATE; COMMIT;").isOk());
}

void AccountRepositoryTest::likeUpdateTouchesOnlyTheCachedSong()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());
    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-a"), accountSong(QStringLiteral("r1"), QStringLiteral("A"))));
    QVERIFY(accounts.ensureAccountTrack(QStringLiteral("owner-b"), accountSong(QStringLiteral("r1"), QStringLiteral("A"))));

    QVERIFY(accounts.updateAccountTrackLike(QStringLiteral("owner-a"), QStringLiteral("r1"), true));
    QCOMPARE(library.text("SELECT group_concat(Liked) FROM (SELECT AccountId || '=' || IsLiked AS Liked FROM AccountTracks "
                          "ORDER BY AccountId);"),
             QStringLiteral("owner-a=1,owner-b=0"));
    QVERIFY(accounts.updateAccountTrackLike(QStringLiteral("owner-a"), QStringLiteral("unknown"), true));
    QVERIFY(!accounts.updateAccountTrackLike(QStringLiteral("owner-a"), QString(), true));
}

QTEST_GUILESS_MAIN(AccountRepositoryTest)
#include "tst_account_repository.moc"
