#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"

using lmp::core::FailureKind;
using lmp::core::Track;
using lmp::core::db::LibraryQueries;
using lmp::core::db::TrackRepository;

namespace {

Track localTrack(const QString& title)
{
    Track track;
    track.title = title;
    track.artist = QStringLiteral("Test Artist");
    track.album = QStringLiteral("Test Album");
    track.genre = QStringLiteral("Test Genre");
    track.durationSeconds = 200;
    track.durationText = QStringLiteral("3:20");
    track.filePath = QStringLiteral("C:/Music/") + title + QStringLiteral(".flac");
    track.dateAdded = QStringLiteral("2026-01-02");
    track.dateAddedSortKey = 100;
    return track;
}

QStringList titles(const QList<Track>& tracks)
{
    QStringList result;
    for (const Track& track : tracks) {
        result << track.title;
    }
    return result;
}

} // namespace

class TrackRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void insertReturnsIdAndReadsBack();
    void upsertOnTheSameSourceKeyUpdates();
    void emptySourceKeyIsRefused();
    void remoteUpsertClearsUnsafeAddresses();
    void completeLocalScanDeactivatesMissingFiles();
    void playbackStatisticsAccumulate();
    void likedFlagRoundTrips();
    void resumePositionIsStoredAndCleared();
    void clearAllUserDataEmptiesEverything();
};

void TrackRepositoryTest::insertReturnsIdAndReadsBack()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());

    Track track = localTrack(QStringLiteral("First Song"));
    track.catalogId = QStringLiteral("cat-1");
    const auto inserted = tracks.upsertLocalTrack(track, QStringLiteral("local|first"));
    QVERIFY(inserted.isOk());
    QVERIFY(inserted.value() > 0);

    LibraryQueries queries(library.database());
    const QList<Track> loaded = queries.loadTracks();
    QCOMPARE(loaded.size(), 1);
    const Track& row = loaded.front();
    QCOMPARE(row.rowId, inserted.value());
    QCOMPARE(row.title, QStringLiteral("First Song"));
    QCOMPARE(row.artist, QStringLiteral("Test Artist"));
    QCOMPARE(row.album, QStringLiteral("Test Album"));
    QCOMPARE(row.genre, QStringLiteral("Test Genre"));
    QCOMPARE(row.durationSeconds, 200.0);
    QCOMPARE(row.durationText, QStringLiteral("3:20"));
    QCOMPARE(row.filePath, QStringLiteral("C:/Music/First Song.flac"));
    QCOMPARE(row.dateAdded, QStringLiteral("2026-01-02"));
    QCOMPARE(row.sourceKind, QStringLiteral("local"));
    QCOMPARE(row.origin, QString());
    QCOMPARE(row.sourceLabel, QStringLiteral("Local"));
    QCOMPARE(row.catalogId, QStringLiteral("cat-1"));
    QCOMPARE(row.index, 1);
    QCOMPARE(library.number("SELECT IsActive FROM Tracks;"), 1);
}

void TrackRepositoryTest::upsertOnTheSameSourceKeyUpdates()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());

    Track track = localTrack(QStringLiteral("Before"));
    track.catalogId = QStringLiteral("cat-1");
    track.albumCatalogId = QStringLiteral("album-1");
    const auto first = tracks.upsertLocalTrack(track, QStringLiteral("local|same"));
    QVERIFY(first.isOk());

    Track updated = localTrack(QStringLiteral("After"));
    updated.albumCatalogId = QStringLiteral("album-2");
    const auto second = tracks.upsertLocalTrack(updated, QStringLiteral("local|same"));
    QVERIFY(second.isOk());
    QCOMPARE(second.value(), first.value());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 1);
    QCOMPARE(library.text("SELECT Title FROM Tracks;"), QStringLiteral("After"));
    // An update without a catalogue id keeps the stored one; a new one replaces it.
    QCOMPARE(library.text("SELECT RemoteCatalogId FROM Tracks;"), QStringLiteral("cat-1"));
    QCOMPARE(library.text("SELECT AlbumCatalogId FROM Tracks;"), QStringLiteral("album-2"));
}

void TrackRepositoryTest::emptySourceKeyIsRefused()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());

    const auto refused = tracks.upsertLocalTrack(localTrack(QStringLiteral("Nowhere")), QString());
    QVERIFY(!refused.isOk());
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 0);
    // A key that is not valid text (a lone surrogate) is refused too, rather
    // than stored under a NULL key that the next scan adds again.
    const QString broken = QStringLiteral("local|c:/music/") + QChar(0xD800) + QStringLiteral(".mp3");
    QVERIFY(!tracks.upsertLocalTrack(localTrack(QStringLiteral("Broken")), broken).isOk());
    QVERIFY(!tracks.upsertLocalTrack(localTrack(QStringLiteral("Broken")), broken).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 0);
    // The void operations skip an empty key without failing.
    QVERIFY(tracks.recordPlayback(QString(), 1).isOk());
    QVERIFY(tracks.setLiked(QString(), true).isOk());
    QVERIFY(!tracks.isLiked(QString()));
}

void TrackRepositoryTest::remoteUpsertClearsUnsafeAddresses()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());

    Track unsafe;
    unsafe.title = QStringLiteral("Signed");
    unsafe.sourceUrl = QStringLiteral("https://media.example.test/a.mp3?access_token=secret");
    unsafe.artworkUrl = QStringLiteral("https://images.example.test/a.jpg?signature=abc");
    unsafe.filePath = QStringLiteral("https://media.example.test/a.mp3");
    QVERIFY(tracks.upsertRemoteTrack(unsafe, QStringLiteral("remote|tag-a|one")).isOk());
    QCOMPARE(library.text("SELECT SourceUrl FROM Tracks WHERE SourceKey='remote|tag-a|one';"), QString());
    QCOMPARE(library.text("SELECT ArtworkUrl FROM Tracks WHERE SourceKey='remote|tag-a|one';"), QString());
    // A remote row never stores a file path, and an empty origin is stored as "remote".
    QCOMPARE(library.text("SELECT FilePath FROM Tracks WHERE SourceKey='remote|tag-a|one';"), QString());
    QCOMPARE(library.text("SELECT Provider FROM Tracks WHERE SourceKey='remote|tag-a|one';"), QStringLiteral("remote"));

    Track safe;
    safe.title = QStringLiteral("Plain");
    safe.origin = QStringLiteral("tag-b");
    safe.filePath = QStringLiteral("https://media.example.test/b.mp3");
    safe.artworkUrl = QStringLiteral("https://images.example.test/b.jpg");
    QVERIFY(tracks.upsertRemoteTrack(safe, QStringLiteral("remote|tag-b|two")).isOk());
    // Without a source address the file path stands in for it.
    QCOMPARE(library.text("SELECT SourceUrl FROM Tracks WHERE SourceKey='remote|tag-b|two';"),
             QStringLiteral("https://media.example.test/b.mp3"));
    QCOMPARE(library.text("SELECT ArtworkUrl FROM Tracks WHERE SourceKey='remote|tag-b|two';"),
             QStringLiteral("https://images.example.test/b.jpg"));
    QCOMPARE(library.text("SELECT Provider FROM Tracks WHERE SourceKey='remote|tag-b|two';"), QStringLiteral("tag-b"));

    // A local row keeps its path, minus the obsolete server credential.
    Track local = localTrack(QStringLiteral("Relay"));
    local.filePath = QStringLiteral("https://media.example.test/v1/stream/x?access_token=k&id=7");
    QVERIFY(tracks.upsertLocalTrack(local, QStringLiteral("local|relay")).isOk());
    QCOMPARE(library.text("SELECT FilePath FROM Tracks WHERE SourceKey='local|relay';"),
             QStringLiteral("https://media.example.test/v1/stream/x?id=7"));

    // In server-key mode the remote rows are listed; one without a path plays its source.
    QVERIFY(library.connection().exec("UPDATE ActiveAccountContext SET RemoteMode='ApiKey';").isOk());
    LibraryQueries queries(library.database());
    bool sawPlain = false;
    for (const Track& track : queries.loadTracks()) {
        if (track.title == QStringLiteral("Plain")) {
            sawPlain = true;
            QCOMPARE(track.filePath, QStringLiteral("https://media.example.test/b.mp3"));
            QCOMPARE(track.sourceLabel, QStringLiteral("Remote"));
        }
    }
    QVERIFY(sawPlain);
    QCOMPARE(queries.loadTracks(false).size(), 1);
}

void TrackRepositoryTest::completeLocalScanDeactivatesMissingFiles()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Kept A")), QStringLiteral("local|a")).isOk());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Kept B")), QStringLiteral("local|b")).isOk());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Gone")), QStringLiteral("local|c")).isOk());
    Track remote;
    remote.title = QStringLiteral("Remote");
    remote.sourceUrl = QStringLiteral("https://media.example.test/r.mp3");
    QVERIFY(tracks.upsertRemoteTrack(remote, QStringLiteral("remote|tag-a|r")).isOk());

    tracks.beginLocalScan();
    // Starting a scan changes nothing.
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE IsActive=1;"), 4);
    QVERIFY(tracks.completeLocalScan({QStringLiteral("local|a"), QStringLiteral("local|b")}).isOk());

    // Rows are deactivated, not deleted, and remote rows are untouched.
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 4);
    QCOMPARE(library.number("SELECT IsActive FROM Tracks WHERE SourceKey='local|c';"), 0);
    QCOMPARE(library.number("SELECT IsActive FROM Tracks WHERE SourceKey='remote|tag-a|r';"), 1);
    LibraryQueries queries(library.database());
    QCOMPARE(titles(queries.loadTracks(false, true)).size(), 2);
    QCOMPARE(queries.loadTracks(false, false).size(), 3);

    // Rescanning a file brings it back.
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Gone")), QStringLiteral("local|c")).isOk());
    QCOMPARE(library.number("SELECT IsActive FROM Tracks WHERE SourceKey='local|c';"), 1);
}

void TrackRepositoryTest::playbackStatisticsAccumulate()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Often")), QStringLiteral("local|often")).isOk());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Once")), QStringLiteral("local|once")).isOk());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Never")), QStringLiteral("local|never")).isOk());
    QCOMPARE(tracks.maxLastPlayedOrder(), quint64{0});

    QVERIFY(tracks.recordPlayback(QStringLiteral("local|often"), 1).isOk());
    QVERIFY(tracks.recordPlayback(QStringLiteral("local|often"), 2).isOk());
    QVERIFY(tracks.recordPlayback(QStringLiteral("local|once"), 3).isOk());
    QCOMPARE(library.number("SELECT PlayCount FROM Tracks WHERE SourceKey='local|often';"), 2);
    QCOMPARE(library.number("SELECT LastPlayedExact FROM Tracks WHERE SourceKey='local|often';"), 1);
    QCOMPARE(tracks.maxLastPlayedOrder(), quint64{3});

    LibraryQueries queries(library.database());
    QCOMPARE(titles(queries.loadHistoryTracks()), (QStringList{QStringLiteral("Once"), QStringLiteral("Often")}));
    QCOMPARE(titles(queries.loadMostPlayedTracks()), (QStringList{QStringLiteral("Often"), QStringLiteral("Once")}));

    // Merging only ever raises the stored values.
    QVERIFY(tracks.mergePlaybackStats(QStringLiteral("local|often"), 1, 1).isOk());
    QCOMPARE(library.number("SELECT PlayCount FROM Tracks WHERE SourceKey='local|often';"), 2);
    QVERIFY(tracks.mergePlaybackStats(QStringLiteral("local|never"), 5, 10).isOk());
    QCOMPARE(library.number("SELECT PlayCount FROM Tracks WHERE SourceKey='local|never';"), 5);
    QCOMPARE(tracks.maxLastPlayedOrder(), quint64{10});
    QCOMPARE(titles(queries.loadMostPlayedTracks()).front(), QStringLiteral("Never"));

    const auto stats = queries.loadPlaybackStats();
    QCOMPARE(stats.size(), 3);
    for (const auto& stat : stats) {
        if (stat.sourceKey == QStringLiteral("local|never")) {
            QCOMPARE(stat.playCount, quint32{5});
            QCOMPARE(stat.lastPlayedOrder, quint64{10});
            QCOMPARE(stat.track.title, QStringLiteral("Never"));
        }
    }
}

void TrackRepositoryTest::likedFlagRoundTrips()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Loved")), QStringLiteral("local|loved")).isOk());

    QVERIFY(!tracks.isLiked(QStringLiteral("local|loved")));
    QVERIFY(tracks.setLiked(QStringLiteral("local|loved"), true).isOk());
    QVERIFY(tracks.isLiked(QStringLiteral("local|loved")));
    LibraryQueries queries(library.database());
    QVERIFY(queries.loadTracks().front().liked);
    QVERIFY(tracks.setLiked(QStringLiteral("local|loved"), false).isOk());
    QVERIFY(!tracks.isLiked(QStringLiteral("local|loved")));
    QVERIFY(!tracks.isLiked(QStringLiteral("local|unknown")));
}

void TrackRepositoryTest::resumePositionIsStoredAndCleared()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Long")), QStringLiteral("local|long")).isOk());
    LibraryQueries queries(library.database());

    QVERIFY(tracks.recordPlaybackPosition(QStringLiteral("local|long"), 42.5).isOk());
    QCOMPARE(queries.loadTracks().front().resumePositionSeconds, 42.5);
    QVERIFY(tracks.recordPlaybackPosition(QStringLiteral("local|long"), -3).isOk());
    QCOMPARE(queries.loadTracks().front().resumePositionSeconds, 0.0);
}

void TrackRepositoryTest::clearAllUserDataEmptiesEverything()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QVERIFY(tracks.upsertLocalTrack(localTrack(QStringLiteral("Wiped")), QStringLiteral("local|wiped")).isOk());
    QVERIFY(library.connection()
                .exec("INSERT INTO Playlists (PlaylistKey, Title) VALUES ('manual|1', 'List');"
                      "INSERT INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) VALUES (1, 1, 1);"
                      "UPDATE ActiveAccountContext SET RemoteMode='Account', AccountId='account-a';")
                .isOk());

    QVERIFY(tracks.clearAllUserData().isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Playlists;"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaylistTracks;"), 0);
    QCOMPARE(library.text("SELECT RemoteMode FROM ActiveAccountContext;"), QStringLiteral("LocalOnly"));
    // The id counters restart.
    const auto again = tracks.upsertLocalTrack(localTrack(QStringLiteral("Fresh")), QStringLiteral("local|fresh"));
    QVERIFY(again.isOk());
    QCOMPARE(again.value(), qint64{1});
}

QTEST_GUILESS_MAIN(TrackRepositoryTest)
#include "tst_track_repository.moc"
