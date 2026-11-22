#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"

using lmp::core::FailureKind;
using lmp::core::Track;
using lmp::core::db::CollectionRepository;
using lmp::core::db::GroupQueries;
using lmp::core::db::LibraryQueries;
using lmp::core::db::TrackQuery;
using lmp::core::db::TrackRepository;

namespace {

// Adds local songs titled `names` and returns their ids in the same order.
QList<qint64> addSongs(TempLibrary& library, const QStringList& names)
{
    TrackRepository tracks(library.database());
    QList<qint64> ids;
    for (const QString& name : names) {
        Track track;
        track.title = name;
        track.artist = QStringLiteral("Test Artist");
        const auto id = tracks.upsertLocalTrack(track, QStringLiteral("local|") + name);
        ids << (id.isOk() ? id.value() : -1);
    }
    return ids;
}

QString keyOfPlaylist(TempLibrary& library, qint64 id)
{
    const QByteArray sql = "SELECT PlaylistKey FROM Playlists WHERE Id=" + QByteArray::number(id) + ";";
    return library.text(sql.constData());
}

QStringList playlistOrder(TempLibrary& library, const QString& playlistKey)
{
    TrackQuery query;
    query.groupKind = QStringLiteral("playlist");
    query.groupKey = playlistKey;
    query.sort.clear();
    QStringList titles;
    for (const Track& track : LibraryQueries(library.database()).loadTracksForQuery(query)) {
        titles << track.title;
    }
    return titles;
}

} // namespace

class CollectionRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void createPlaylistUsesManualKeyAndDefaults();
    void addTracksAppendsInOrder();
    void reorderStoresTheWholeOrder();
    void removeTracksFromPlaylist();
    void moveTracksBetweenPlaylists();
    void renameAndDeletePlaylist();
    void albumCollectionMembersAndDelete();
    void upsertClearsUnsafeArtwork();
    void playlistCardsListLocalPlaylists();
};

void CollectionRepositoryTest::createPlaylistUsesManualKeyAndDefaults()
{
    TempLibrary library;
    QVERIFY(library.ok());
    CollectionRepository collections(library.database());

    const auto created = collections.createPlaylist(QString());
    QVERIFY(created.isOk());
    const QString key = keyOfPlaylist(library, created.value());
    QVERIFY(key.startsWith(QStringLiteral("manual|")));
    QCOMPARE(key.count(QLatin1Char('|')), 2);
    QCOMPARE(library.text("SELECT Title FROM Playlists;"), QStringLiteral("Untitled Playlist"));
    QCOMPARE(library.text("SELECT Description FROM Playlists;"), QStringLiteral("Add songs from your library."));
    QCOMPARE(library.text("SELECT Provider FROM Playlists;"), QStringLiteral("manual"));
    QCOMPARE(library.text("SELECT SourceLabel FROM Playlists;"), QStringLiteral("Manual"));

    // Two playlists with one title still get different keys.
    const auto first = collections.createPlaylist(QStringLiteral("Same"));
    const auto second = collections.createPlaylist(QStringLiteral("Same"));
    QVERIFY(first.isOk() && second.isOk());
    QVERIFY(first.value() != second.value());

    const auto refused = collections.upsertPlaylist(Track(), QString());
    QVERIFY(!refused.isOk());
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
}

void CollectionRepositoryTest::addTracksAppendsInOrder()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids =
        addSongs(library, {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    CollectionRepository collections(library.database());
    const auto playlist = collections.createPlaylist(QStringLiteral("Mix"));
    QVERIFY(playlist.isOk());
    const QString key = keyOfPlaylist(library, playlist.value());

    QVERIFY(collections.addTrackToPlaylist(key, ids.at(1)).isOk());
    QVERIFY(collections.addTracksToPlaylist(key, {ids.at(2), ids.at(1), ids.at(0)}).isOk());
    QCOMPARE(playlistOrder(library, key), (QStringList{QStringLiteral("Two"), QStringLiteral("Three"), QStringLiteral("One")}));
    QCOMPARE(library.number("SELECT MAX(TrackOrder) FROM PlaylistTracks;"), 3);

    QVERIFY(!collections.addTracksToPlaylist(QStringLiteral("manual|missing"), {ids.at(0)}).isOk());
    QVERIFY(!collections.addTracksToPlaylist(key, {}).isOk());
}

void CollectionRepositoryTest::reorderStoresTheWholeOrder()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids = addSongs(library, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")});
    CollectionRepository collections(library.database());
    const auto playlist = collections.createPlaylist(QStringLiteral("Order"));
    QVERIFY(playlist.isOk());
    const QString key = keyOfPlaylist(library, playlist.value());
    QVERIFY(collections.addTracksToPlaylist(key, ids).isOk());

    QVERIFY(collections.reorderPlaylistTracks(key, {ids.at(2), ids.at(0), ids.at(1)}).isOk());
    QCOMPARE(playlistOrder(library, key), (QStringList{QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));
    QCOMPARE(library.text("SELECT GROUP_CONCAT(TrackOrder) FROM (SELECT TrackOrder FROM PlaylistTracks ORDER BY TrackOrder);"),
             QStringLiteral("1,2,3"));

    // An id that is not a song fails the replace; the read-back reports it and the old order stays.
    QVERIFY(!collections.reorderPlaylistTracks(key, {ids.at(1), 999}).isOk());
    QCOMPARE(playlistOrder(library, key), (QStringList{QStringLiteral("C"), QStringLiteral("A"), QStringLiteral("B")}));
    QVERIFY(!collections.reorderPlaylistTracks(QStringLiteral("manual|missing"), ids).isOk());
}

void CollectionRepositoryTest::removeTracksFromPlaylist()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids =
        addSongs(library, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")});
    CollectionRepository collections(library.database());
    const auto playlist = collections.createPlaylist(QStringLiteral("Trim"));
    QVERIFY(playlist.isOk());
    const QString key = keyOfPlaylist(library, playlist.value());
    QVERIFY(collections.addTracksToPlaylist(key, ids).isOk());

    QVERIFY(collections.removeTrackFromPlaylist(key, ids.at(0)).isOk());
    // Removing a song that is not there still succeeds, as before.
    QVERIFY(collections.removeTrackFromPlaylist(key, ids.at(0)).isOk());
    QVERIFY(collections.removeTracksFromPlaylist(key, {ids.at(2), ids.at(2), 0}).isOk());
    QCOMPARE(playlistOrder(library, key), (QStringList{QStringLiteral("B"), QStringLiteral("D")}));
    // Nothing removed fails and changes nothing.
    QVERIFY(!collections.removeTracksFromPlaylist(key, {ids.at(0)}).isOk());
    QCOMPARE(playlistOrder(library, key), (QStringList{QStringLiteral("B"), QStringLiteral("D")}));
}

void CollectionRepositoryTest::moveTracksBetweenPlaylists()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids =
        addSongs(library, {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D")});
    CollectionRepository collections(library.database());
    const auto from = collections.createPlaylist(QStringLiteral("From"));
    const auto to = collections.createPlaylist(QStringLiteral("To"));
    QVERIFY(from.isOk() && to.isOk());
    const QString fromKey = keyOfPlaylist(library, from.value());
    const QString toKey = keyOfPlaylist(library, to.value());
    QVERIFY(collections.addTracksToPlaylist(fromKey, {ids.at(0), ids.at(1), ids.at(2)}).isOk());
    QVERIFY(collections.addTracksToPlaylist(toKey, {ids.at(3)}).isOk());

    QVERIFY(collections.moveTracksBetweenPlaylists(fromKey, toKey, {ids.at(2), ids.at(0)}).isOk());
    QCOMPARE(playlistOrder(library, fromKey), (QStringList{QStringLiteral("B")}));
    // Moved songs land at the end of the target, in the order asked for.
    QCOMPARE(playlistOrder(library, toKey), (QStringList{QStringLiteral("D"), QStringLiteral("C"), QStringLiteral("A")}));

    QVERIFY(!collections.moveTracksBetweenPlaylists(fromKey, fromKey, {ids.at(1)}).isOk());
    // A song that is not in the source moves nothing and fails.
    QVERIFY(!collections.moveTracksBetweenPlaylists(fromKey, toKey, {ids.at(3)}).isOk());
    QCOMPARE(playlistOrder(library, fromKey), (QStringList{QStringLiteral("B")}));
}

void CollectionRepositoryTest::renameAndDeletePlaylist()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids = addSongs(library, {QStringLiteral("A")});
    CollectionRepository collections(library.database());
    const auto playlist = collections.createPlaylist(QStringLiteral("Old"));
    QVERIFY(playlist.isOk());
    const QString key = keyOfPlaylist(library, playlist.value());
    QVERIFY(collections.addTracksToPlaylist(key, ids).isOk());

    QVERIFY(collections.renamePlaylist(key, QStringLiteral("New")).isOk());
    QCOMPARE(library.text("SELECT Title FROM Playlists;"), QStringLiteral("New"));
    QVERIFY(collections.renamePlaylist(key, QString()).isOk());
    QCOMPARE(library.text("SELECT Title FROM Playlists;"), QStringLiteral("Untitled Playlist"));
    QVERIFY(!collections.renamePlaylist(QStringLiteral("manual|missing"), QStringLiteral("X")).isOk());

    QVERIFY(collections.deletePlaylist(key).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Playlists;"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaylistTracks;"), 0);
    // The song itself stays in the library.
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 1);
    QVERIFY(!collections.deletePlaylist(key).isOk());
}

void CollectionRepositoryTest::albumCollectionMembersAndDelete()
{
    TempLibrary library;
    QVERIFY(library.ok());
    TrackRepository tracks(library.database());
    QList<qint64> remoteIds;
    for (const QString& name : {QStringLiteral("R1"), QStringLiteral("R2"), QStringLiteral("R3")}) {
        Track track;
        track.title = name;
        track.sourceUrl = QStringLiteral("https://media.example.test/") + name;
        const auto id = tracks.upsertRemoteTrack(track, QStringLiteral("remote|tag-a|") + name);
        QVERIFY(id.isOk());
        remoteIds << id.value();
    }
    CollectionRepository collections(library.database());

    // A manual album keeps its default caption and accepts members in order.
    const auto manual = collections.createAlbumCollection(QString());
    QVERIFY(manual.isOk());
    QCOMPARE(library.text("SELECT Title FROM Albums;"), QStringLiteral("Untitled Album"));
    QVERIFY(collections.replaceAlbumCollectionTracks(manual.value(), {remoteIds.at(1), 0, remoteIds.at(0)}).isOk());
    TrackQuery query;
    query.groupKind = QStringLiteral("album-collection");
    query.groupKey = library.text("SELECT AlbumKey FROM Albums;");
    query.sort.clear();
    QStringList order;
    for (const Track& track : LibraryQueries(library.database()).loadTracksForQuery(query)) {
        order << track.title;
    }
    QCOMPARE(order, (QStringList{QStringLiteral("R2"), QStringLiteral("R1")}));

    // Deleting an imported album removes its remote songs that nothing else holds.
    Track imported;
    imported.title = QStringLiteral("Imported");
    imported.sourceKind = QStringLiteral("remote");
    const auto album = collections.upsertAlbumCollection(imported, QStringLiteral("album|tag-a|imported"));
    QVERIFY(album.isOk());
    QVERIFY(collections.replaceAlbumCollectionTracks(album.value(), remoteIds).isOk());
    QVERIFY(collections.deleteAlbumCollection(QStringLiteral("album|tag-a|imported")).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Albums;"), 1);
    // R1 and R2 are still in the manual album; only R3 goes.
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 2);
    QCOMPARE(library.text("SELECT COUNT(*) FROM Tracks WHERE Title='R3';"), QStringLiteral("0"));

    // Deleting a manual album leaves its songs alone.
    QVERIFY(collections.deleteAlbumCollection(query.groupKey).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks;"), 2);
    QCOMPARE(library.number("SELECT COUNT(*) FROM AlbumTracks;"), 0);
    QVERIFY(!collections.deleteAlbumCollection(query.groupKey).isOk());
}

void CollectionRepositoryTest::upsertClearsUnsafeArtwork()
{
    TempLibrary library;
    QVERIFY(library.ok());
    CollectionRepository collections(library.database());

    Track album;
    album.title = QStringLiteral("Signed Art");
    album.sourceKind = QStringLiteral("remote");
    album.artworkUrl = QStringLiteral("https://images.example.test/a.jpg?token=abc");
    QVERIFY(collections.upsertAlbumCollection(album, QStringLiteral("album|one")).isOk());
    QCOMPARE(library.text("SELECT ArtworkUrl FROM Albums;"), QString());
    // The label falls back to "Album".
    QCOMPARE(library.text("SELECT SourceLabel FROM Albums;"), QStringLiteral("Album"));

    Track playlist;
    playlist.title = QStringLiteral("Safe Art");
    playlist.origin = QStringLiteral("tag-a");
    playlist.artworkUrl = QStringLiteral("https://images.example.test/b.jpg");
    const auto first = collections.upsertPlaylist(playlist, QStringLiteral("playlist|one"));
    QVERIFY(first.isOk());
    QCOMPARE(library.text("SELECT ArtworkUrl FROM Playlists;"), QStringLiteral("https://images.example.test/b.jpg"));
    // Upserting the same key updates in place.
    playlist.title = QStringLiteral("Renamed");
    const auto second = collections.upsertPlaylist(playlist, QStringLiteral("playlist|one"));
    QVERIFY(second.isOk());
    QCOMPARE(second.value(), first.value());
    QCOMPARE(library.text("SELECT Title FROM Playlists;"), QStringLiteral("Renamed"));
}

void CollectionRepositoryTest::playlistCardsListLocalPlaylists()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const QList<qint64> ids = addSongs(library, {QStringLiteral("A"), QStringLiteral("B")});
    CollectionRepository collections(library.database());
    const auto playlist = collections.createPlaylist(QStringLiteral("Cards"));
    QVERIFY(playlist.isOk());
    const QString key = keyOfPlaylist(library, playlist.value());
    QVERIFY(collections.addTracksToPlaylist(key, ids).isOk());

    GroupQueries groups(library.database());
    const QList<Track> cards = groups.loadPlaylists();
    QCOMPARE(cards.size(), 1);
    QCOMPARE(cards.front().title, QStringLiteral("Cards"));
    QCOMPARE(cards.front().sourceUrl, key);
    QCOMPARE(cards.front().sourceKind, QStringLiteral("playlist"));
    QCOMPARE(cards.front().sourceLabel, QStringLiteral("On this PC"));
    QCOMPARE(cards.front().artist, QStringLiteral("Add songs from your library."));
    QCOMPARE(cards.front().trackCount, 2);
    QCOMPARE(cards.front().rowId, playlist.value());
    QCOMPARE(groups.loadRecentPlaylists(5).size(), 1);
    QVERIFY(groups.loadRecentPlaylists(0).isEmpty());
}

QTEST_GUILESS_MAIN(CollectionRepositoryTest)
#include "tst_collection_repository.moc"
