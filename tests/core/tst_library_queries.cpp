#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/AccountPlaylistPage.h"
#include "core/db/GroupQueries.h"
#include "core/db/LibraryQueries.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"

using lmp::core::Track;
using lmp::core::db::AccountPlaylistTrackSource;
using lmp::core::db::GroupQueries;
using lmp::core::db::LibraryQueries;
using lmp::core::db::LibraryStats;
using lmp::core::db::TrackPage;
using lmp::core::db::TrackQuery;
using lmp::core::db::TrackRepository;

namespace {

struct Song {
    QString title;
    QString artist;
    QString album;
    QString genre;
    double seconds;
    double added;
};

// Five local songs over two albums, two artists and two genres, plus one
// untagged file.
bool addLibrary(TempLibrary& library)
{
    const QList<Song> songs{
        {QStringLiteral("Alpha"), QStringLiteral("Ada"), QStringLiteral("North"), QStringLiteral("Rock"), 100, 5},
        {QStringLiteral("Beta"), QStringLiteral("Ada"), QStringLiteral("north"), QStringLiteral("Rock"), 200, 4},
        {QStringLiteral("Gamma"), QStringLiteral("Ada"), QStringLiteral("South"), QStringLiteral("Jazz"), 300, 3},
        {QStringLiteral("Delta 50%"), QStringLiteral("Bo"), QStringLiteral("South"), QStringLiteral("Jazz"), 400, 2},
        {QStringLiteral("Epsilon"), QStringLiteral("Bo"), QStringLiteral("North"), QStringLiteral("Rock"), 500, 1},
        {QStringLiteral("Untagged"), QString(), QString(), QString(), 600, 0},
    };
    TrackRepository tracks(library.database());
    for (const Song& song : songs) {
        Track track;
        track.title = song.title;
        track.artist = song.artist;
        track.album = song.album;
        track.genre = song.genre;
        track.durationSeconds = song.seconds;
        track.dateAddedSortKey = song.added;
        if (!tracks.upsertLocalTrack(track, QStringLiteral("local|") + song.title).isOk()) {
            return false;
        }
    }
    return true;
}

QStringList titles(const QList<Track>& tracks)
{
    QStringList result;
    for (const Track& track : tracks) {
        result << track.title;
    }
    return result;
}

class FakeAccountPlaylists : public AccountPlaylistTrackSource {
public:
    [[nodiscard]] QList<Track> loadAccountPlaylistTracks(const QString& accountId,
                                                         const QString& playlistId) const override
    {
        if (accountId != QStringLiteral("account-a") || playlistId != QStringLiteral("list-1")) {
            return {};
        }
        QList<Track> tracks;
        for (const QString& name : {QStringLiteral("Zulu"), QStringLiteral("Echo"), QStringLiteral("Mike")}) {
            Track track;
            track.title = name;
            track.durationSeconds = 10;
            tracks << track;
        }
        return tracks;
    }
};

} // namespace

class LibraryQueriesTest : public QObject {
    Q_OBJECT

private slots:
    void groupCardsEndToEnd();
    void groupDetailMatchesTheCard();
    void flatPageFiltersSortsAndPages();
    void searchEscapesWildcards();
    void statsAndTopGenres();
    void recentlyAddedSkipsUndatedSongs();
    void accountPlaylistPageUsesTheSource();
};

void LibraryQueriesTest::groupCardsEndToEnd()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    GroupQueries groups(library.database());

    const QList<Track> albums = groups.loadAlbums();
    QCOMPARE(albums.size(), 3);
    QCOMPARE(albums.at(0).title.toLower(), QStringLiteral("north"));
    QCOMPARE(albums.at(0).trackCount, 3);
    QCOMPARE(albums.at(0).sourceKind, QStringLiteral("album"));
    QCOMPARE(albums.at(0).sourceUrl, albums.at(0).title);
    QCOMPARE(albums.at(0).sourceLabel, QStringLiteral("Local"));
    QCOMPARE(albums.at(0).artist, QStringLiteral("Ada"));
    QCOMPARE(albums.at(1).title, QStringLiteral("South"));
    QCOMPARE(albums.at(2).title, QStringLiteral("Unknown Album"));

    const QList<Track> artists = groups.loadArtists();
    QCOMPARE(titles(artists), (QStringList{QStringLiteral("Ada"), QStringLiteral("Bo"), QStringLiteral("Unknown Artist")}));
    QCOMPARE(artists.at(0).artist, QStringLiteral("3 songs - 2 albums"));
    QVERIFY(artists.at(0).inLibrary);

    const QList<Track> genres = groups.loadGenres();
    QCOMPARE(titles(genres), (QStringList{QStringLiteral("Rock"), QStringLiteral("Jazz"), QStringLiteral("Unknown Genre")}));
    QCOMPARE(genres.at(2).artist, QStringLiteral("1 song"));
    QCOMPARE(genres.at(0).artist, QStringLiteral("3 songs"));
}

void LibraryQueriesTest::groupDetailMatchesTheCard()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    LibraryQueries queries(library.database());

    QCOMPARE(titles(queries.loadTracksForGroup(QStringLiteral("Album"), QStringLiteral("NORTH"))),
             (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Epsilon")}));
    QCOMPARE(titles(queries.loadTracksForGroup(QStringLiteral("artist"), QStringLiteral("Unknown Artist"))),
             (QStringList{QStringLiteral("Untagged")}));
    QVERIFY(queries.loadTracksForGroup(QStringLiteral("decade"), QStringLiteral("North")).isEmpty());
    QVERIFY(queries.loadTracksForGroup(QStringLiteral("album"), QString()).isEmpty());
}

void LibraryQueriesTest::flatPageFiltersSortsAndPages()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    TrackRepository tracks(library.database());
    QVERIFY(tracks.setLiked(QStringLiteral("local|Gamma"), true).isOk());
    QVERIFY(tracks.setLiked(QStringLiteral("local|Alpha"), true).isOk());
    LibraryQueries queries(library.database());

    TrackQuery query;
    query.sort = QStringLiteral("Title");
    query.limit = 2;
    query.offset = 1;
    const TrackPage page = queries.loadTrackPage(query);
    QCOMPARE(page.totalCount, 6);
    QCOMPARE(page.totalSeconds, 2100.0);
    QCOMPARE(titles(page.tracks), (QStringList{QStringLiteral("Beta"), QStringLiteral("Delta 50%")}));
    QCOMPARE(page.tracks.at(0).index, 2);

    TrackQuery liked;
    liked.filter = QStringLiteral("Liked");
    liked.sort = QStringLiteral("duration");
    const TrackPage likedPage = queries.loadTrackPage(liked);
    QCOMPARE(likedPage.totalCount, 2);
    QCOMPARE(titles(likedPage.tracks), (QStringList{QStringLiteral("Gamma"), QStringLiteral("Alpha")}));

    TrackQuery grouped;
    grouped.groupKind = QStringLiteral("genre");
    grouped.groupKey = QStringLiteral("jazz");
    grouped.searchText = QStringLiteral("gam");
    QCOMPARE(titles(queries.loadTracksForQuery(grouped)), (QStringList{QStringLiteral("Gamma")}));

    // The default order is newest first.
    QCOMPARE(titles(queries.loadTracks()).front(), QStringLiteral("Alpha"));
    // The account scope holds no local song.
    TrackQuery account;
    account.scope = QStringLiteral("Account");
    QCOMPARE(queries.loadTrackPage(account).totalCount, 0);
}

void LibraryQueriesTest::searchEscapesWildcards()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    LibraryQueries queries(library.database());

    TrackQuery query;
    query.searchText = QStringLiteral("50%");
    QCOMPARE(titles(queries.loadTracksForQuery(query)), (QStringList{QStringLiteral("Delta 50%")}));
    query.searchText = QStringLiteral("%");
    QCOMPARE(queries.loadTracksForQuery(query).size(), 1);
    query.searchText = QStringLiteral("_");
    QVERIFY(queries.loadTracksForQuery(query).isEmpty());
}

void LibraryQueriesTest::statsAndTopGenres()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    TrackRepository tracks(library.database());
    QVERIFY(tracks.recordPlayback(QStringLiteral("local|Gamma"), 1).isOk());
    LibraryQueries queries(library.database());
    GroupQueries groups(library.database());

    const LibraryStats stats = queries.libraryStats();
    QCOMPARE(stats.songCount, 6);
    // Album names are counted as written: North and north are two.
    QCOMPARE(stats.albumCount, 4);
    QCOMPARE(stats.artistCount, 3);
    QCOMPARE(stats.genreCount, 3);
    QCOMPARE(stats.totalSeconds, 2100.0);

    // Listening ranks first; an untagged song is never a genre.
    QCOMPARE(groups.loadTopGenres(5), (QStringList{QStringLiteral("Jazz"), QStringLiteral("Rock")}));
    QCOMPARE(groups.loadTopGenres(1), (QStringList{QStringLiteral("Jazz")}));
    QVERIFY(groups.loadTopGenres(0).isEmpty());
}

void LibraryQueriesTest::recentlyAddedSkipsUndatedSongs()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(addLibrary(library));
    LibraryQueries queries(library.database());

    QCOMPARE(queries.loadRecentlyAddedTracks(0).size(), 5);
    QCOMPARE(titles(queries.loadRecentlyAddedTracks(2)), (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta")}));
}

void LibraryQueriesTest::accountPlaylistPageUsesTheSource()
{
    TempLibrary library;
    QVERIFY(library.ok());
    const FakeAccountPlaylists source;
    LibraryQueries queries(library.database(), &source);

    TrackQuery query;
    query.groupKind = QStringLiteral("playlist");
    query.groupKey = QStringLiteral("account-playlist|list-1");
    query.accountOwnerId = QStringLiteral("account-a");
    query.sort.clear();
    TrackPage page = queries.loadTrackPage(query);
    QCOMPARE(titles(page.tracks), (QStringList{QStringLiteral("Zulu"), QStringLiteral("Echo"), QStringLiteral("Mike")}));
    QCOMPARE(page.totalSeconds, 30.0);

    query.sort = QStringLiteral("title");
    query.offset = 1;
    query.limit = 1;
    page = queries.loadTrackPage(query);
    QCOMPARE(page.totalCount, 3);
    QCOMPARE(titles(page.tracks), (QStringList{QStringLiteral("Mike")}));
    QCOMPARE(page.tracks.front().index, 2);

    query.offset = 0;
    query.limit = 0;
    query.searchText = QStringLiteral("CH");
    QCOMPARE(titles(queries.loadTrackPage(query).tracks), (QStringList{QStringLiteral("Echo")}));

    // Without an owner, or without a source, the page is empty.
    query.accountOwnerId.clear();
    QCOMPARE(queries.loadTrackPage(query).totalCount, 0);
    query.accountOwnerId = QStringLiteral("account-a");
    QCOMPARE(LibraryQueries(library.database()).loadTrackPage(query).totalCount, 0);
}

QTEST_GUILESS_MAIN(LibraryQueriesTest)
#include "tst_library_queries.moc"
