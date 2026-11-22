#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/db/LibraryGroupingSql.h"
#include "core/db/PlaylistBulkInsert.h"
#include "core/db/SqliteConnection.h"

#include <string>

namespace grouping = lmp::core::db::library_grouping_sql;
using lmp::core::db::SqliteConnection;
using lmp::core::db::addTracksToPlaylistTransactional;

namespace {

using GroupList = QList<QPair<QString, int>>;

// Library rows covering local files, account songs reached by play, history,
// like and playlist membership, untouched account rows, server-key rows and
// an inactive file.
constexpr char kGroupingFixtureSql[] =
    "CREATE TABLE EffectiveTracks ("
    "Id INTEGER PRIMARY KEY, SourceKind TEXT, Provider TEXT, RemoteId TEXT, "
    "Album TEXT, Artist TEXT, Genre TEXT, PlayCount INTEGER, "
    "LastPlayedOrder INTEGER, IsLiked INTEGER, IsActive INTEGER, ArtworkUrl TEXT);"
    "CREATE TABLE ActiveAccountContext ("
    "SingletonId INTEGER PRIMARY KEY, RemoteMode TEXT, AccountId TEXT);"
    "CREATE TABLE AccountPlaylistTracks ("
    "AccountId TEXT, PlaylistId TEXT, RemoteId TEXT, TrackOrder INTEGER);"
    "INSERT INTO ActiveAccountContext VALUES (1, 'Account', 'account-a');"
    "INSERT INTO EffectiveTracks VALUES "
    "(1, 'local', 'local', NULL, 'Shared Album', 'Local Artist', 'Rock', 0, 0, 0, 1, ''),"
    "(2, 'local', 'local', NULL, 'shared album', 'local artist', 'rock', 0, 0, 0, 1, ''),"
    "(3, 'local', 'local', NULL, '', '', '', 0, 0, 0, 1, ''),"
    "(4, 'remote', 'account', 'r-played', 'Played Album', 'Played Artist', 'Pop', 1, 0, 0, 1, ''),"
    "(5, 'remote', 'account', 'r-history', 'History Album', 'History Artist', 'Jazz', 0, 1, 0, 1, ''),"
    "(6, 'remote', 'account', 'r-liked', 'Liked Album', 'Liked Artist', 'Soul', 0, 0, 1, 1, ''),"
    "(7, 'remote', 'account', 'r-playlist', 'Playlist Album', 'Playlist Artist', 'Folk', 0, 0, 0, 1, ''),"
    "(8, 'remote', 'account', 'r-inert', 'Inert Album', 'Inert Artist', 'Ambient', 0, 0, 0, 1, ''),"
    "(9, 'remote', 'account', 'r-blank', '', '', '', 1, 0, 0, 1, ''),"
    "(10, 'remote', 'account', 'r-unknown', 'Unknown Album', 'Unknown Artist', 'Unknown Genre', 1, 0, 0, 1, ''),"
    "(11, 'remote', 'api-key', 'r-api', 'API Album', 'API Artist', 'Dance', 1, 1, 1, 1, ''),"
    "(12, 'local', 'local', NULL, 'Inactive Album', 'Inactive Artist', 'Metal', 0, 0, 0, 0, '');"
    "INSERT INTO AccountPlaylistTracks VALUES ('account-a', 'playlist-a', 'r-playlist', 0);";

constexpr char kCollectionFixtureSql[] =
    "CREATE TABLE Tracks("
    "Id INTEGER PRIMARY KEY, Title TEXT, Artist TEXT, Album TEXT,"
    "DurationSeconds REAL, DateAddedSortKey REAL, IsActive INTEGER);"
    "CREATE TABLE Playlists(Id INTEGER PRIMARY KEY, PlaylistKey TEXT);"
    "CREATE TABLE PlaylistTracks(PlaylistId INTEGER, TrackId INTEGER, TrackOrder INTEGER);"
    "INSERT INTO Tracks VALUES"
    "(1,'Zephyr','Ada','Night Drive',240,300,1),"
    "(2,'Anchor','Ada','Night Drive',100,100,1),"
    "(3,'Marigold','Bo','Sunroom',180,200,1),"
    "(4,'Dormant','Cy','Sunroom',300,400,0);"
    "INSERT INTO Playlists VALUES(1,'mix');"
    "INSERT INTO PlaylistTracks VALUES(1,3,0),(1,1,1),(1,2,2),(1,4,3);";

constexpr char kBulkFixtureSql[] =
    "PRAGMA foreign_keys=ON;"
    "CREATE TABLE Tracks(Id INTEGER PRIMARY KEY);"
    "CREATE TABLE Playlists(Id INTEGER PRIMARY KEY,PlaylistKey TEXT UNIQUE,UpdatedAt INTEGER);"
    "CREATE TABLE PlaylistTracks("
    "PlaylistId INTEGER NOT NULL REFERENCES Playlists(Id),"
    "TrackId INTEGER NOT NULL REFERENCES Tracks(Id),"
    "TrackOrder INTEGER NOT NULL,"
    "UNIQUE(PlaylistId,TrackId));"
    "INSERT INTO Tracks VALUES(1),(2),(3);"
    "INSERT INTO Playlists VALUES(10,'target',0);";

// An in-memory database built from `fixtureSql`; ok() is false when that failed.
class MemoryDatabase {
public:
    explicit MemoryDatabase(const char* fixtureSql)
    {
        m_ok = m_connection.open(QStringLiteral(":memory:")).isOk() && m_connection.exec(fixtureSql).isOk();
    }

    [[nodiscard]] bool ok() const { return m_ok; }
    [[nodiscard]] SqliteConnection& connection() { return m_connection; }

    [[nodiscard]] int scalar(const char* sql)
    {
        auto statement = m_connection.prepare(sql);
        if (!statement || !statement.value().step().valueOr(false)) {
            return -1;
        }
        return static_cast<int>(statement.value().columnInt64(0));
    }

private:
    SqliteConnection m_connection;
    bool m_ok = false;
};

GroupList groupCounts(SqliteConnection& connection, grouping::GroupKind kind)
{
    const std::string sql = grouping::EligibleTracksCte(kind)
        + "SELECT GroupTitle, COUNT(*) FROM EligibleLibraryTracks "
          "WHERE GroupTitle<>'' GROUP BY GroupTitle COLLATE NOCASE "
          "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";
    GroupList groups;
    auto statement = connection.prepare(sql.c_str());
    if (!statement) {
        groups.append({QStringLiteral("<error> ") + statement.failure().detail, -1});
        return groups;
    }
    while (statement.value().step().valueOr(false)) {
        groups.append({statement.value().columnText(0), static_cast<int>(statement.value().columnInt64(1))});
    }
    return groups;
}

QList<int> groupTrackIds(SqliteConnection& connection, grouping::GroupKind kind, const QString& groupTitle)
{
    const std::string sql = "SELECT Id FROM EffectiveTracks WHERE IsActive=1 AND "
        + grouping::GroupMatchPredicate(kind, "?1") + " ORDER BY Id;";
    QList<int> ids;
    auto statement = connection.prepare(sql.c_str());
    if (!statement) {
        ids.append(-1);
        return ids;
    }
    statement.value().bindText(1, groupTitle);
    while (statement.value().step().valueOr(false)) {
        ids.append(static_cast<int>(statement.value().columnInt64(0)));
    }
    return ids;
}

QMap<QString, int> normalizedCounts(const GroupList& groups)
{
    QMap<QString, int> result;
    for (const auto& [title, count] : groups) {
        if (!result.contains(title.toLower())) {
            result.insert(title.toLower(), count);
        }
    }
    return result;
}

QStringList playlistTitles(SqliteConnection& connection, const QString& sort, const QString& search)
{
    const std::string searchClause = grouping::CollectionSearchClause(search, 3);
    const std::string sql = std::string(
        "SELECT t.Title FROM PlaylistTracks pt "
        "JOIN Playlists p ON p.Id=pt.PlaylistId "
        "JOIN Tracks t ON t.Id=pt.TrackId "
        "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)")
        + searchClause + " " + grouping::CollectionOrderClause(sort, "pt.TrackOrder") + ";";
    QStringList titles;
    auto statement = connection.prepare(sql.c_str());
    if (!statement) {
        titles.append(QStringLiteral("<error> ") + statement.failure().detail);
        return titles;
    }
    statement.value().bindText(1, QStringLiteral("mix"));
    statement.value().bindInt64(2, 1);
    if (!searchClause.empty()) {
        statement.value().bindText(3, QStringLiteral("%") + search + QStringLiteral("%"));
    }
    while (statement.value().step().valueOr(false)) {
        titles.append(statement.value().columnText(0));
    }
    return titles;
}

} // namespace

class LibraryGroupingSqlTest : public QObject {
    Q_OBJECT

private slots:
    void accountUnionAndLocalUnknownGroups();
    void cloudUnknownLabelsAreDropped();
    void groupCardsAndDetailsUseTheSameMembership();
    void collectionOrderAndSearch();
    void atomicBulkPlaylistInsertion();
};

void LibraryGroupingSqlTest::accountUnionAndLocalUnknownGroups()
{
    MemoryDatabase database(kGroupingFixtureSql);
    if (!database.ok()) {
        QFAIL("could not build the grouping fixture");
    }
    const GroupList albums = groupCounts(database.connection(), grouping::GroupKind::Album);
    const QMap<QString, int> counts = normalizedCounts(albums);

    // Albums include local tracks and the account cloud union only.
    QCOMPARE(albums.size(), 6);
    // Groups sort by track count before title.
    QVERIFY(albums.front().first.toLower() == QStringLiteral("shared album") && albums.front().second == 2);
    // Case variants share one album group.
    QCOMPARE(counts.value(QStringLiteral("shared album")), 2);
    QCOMPARE(counts.value(QStringLiteral("played album")), 1);
    QCOMPARE(counts.value(QStringLiteral("history album")), 1);
    QCOMPARE(counts.value(QStringLiteral("liked album")), 1);
    QCOMPARE(counts.value(QStringLiteral("playlist album")), 1);
    // An untagged local file keeps the Unknown Album group.
    QCOMPARE(counts.value(QStringLiteral("unknown album")), 1);
    // Unreferenced account catalogue tracks stay out.
    QVERIFY(!counts.contains(QStringLiteral("inert album")));
    // Server-key search tracks do not become account library groups.
    QVERIFY(!counts.contains(QStringLiteral("api album")));
    // Inactive local files stay out.
    QVERIFY(!counts.contains(QStringLiteral("inactive album")));
}

void LibraryGroupingSqlTest::cloudUnknownLabelsAreDropped()
{
    MemoryDatabase database(kGroupingFixtureSql);
    if (!database.ok()) {
        QFAIL("could not build the grouping fixture");
    }
    for (const grouping::GroupKind kind :
         {grouping::GroupKind::Album, grouping::GroupKind::Artist, grouping::GroupKind::Genre}) {
        const QMap<QString, int> counts = normalizedCounts(groupCounts(database.connection(), kind));
        const QString unknown = kind == grouping::GroupKind::Album ? QStringLiteral("unknown album")
            : kind == grouping::GroupKind::Artist                  ? QStringLiteral("unknown artist")
                                                                   : QStringLiteral("unknown genre");
        // An Unknown group contains the local untagged file only.
        QCOMPARE(counts.value(unknown), 1);
    }
}

void LibraryGroupingSqlTest::groupCardsAndDetailsUseTheSameMembership()
{
    MemoryDatabase database(kGroupingFixtureSql);
    if (!database.ok()) {
        QFAIL("could not build the grouping fixture");
    }
    SqliteConnection& connection = database.connection();

    // Detail matching uses the card's case-insensitive album key.
    QCOMPARE(groupTrackIds(connection, grouping::GroupKind::Album, QStringLiteral("SHARED ALBUM")),
             (QList<int>{1, 2}));
    // The Unknown Album detail does not pull in cloud placeholders.
    QCOMPARE(groupTrackIds(connection, grouping::GroupKind::Album, QStringLiteral("Unknown Album")), (QList<int>{3}));
    // A playlist-only account track appears in its derived detail.
    QCOMPARE(groupTrackIds(connection, grouping::GroupKind::Album, QStringLiteral("Playlist Album")), (QList<int>{7}));

    const QList<int> unknownArtist =
        groupTrackIds(connection, grouping::GroupKind::Artist, QStringLiteral("Unknown Artist"));
    const QList<int> unknownGenre = groupTrackIds(connection, grouping::GroupKind::Genre, QStringLiteral("Unknown Genre"));
    // The Unknown Artist and Unknown Genre details contain local untagged files only.
    QCOMPARE(unknownArtist, (QList<int>{3}));
    QCOMPARE(unknownGenre, (QList<int>{3}));
}

// Runs the real clauses against a playlist, so a regression in sort or find
// shows up as wrong rows rather than as a differing SQL string.
void LibraryGroupingSqlTest::collectionOrderAndSearch()
{
    MemoryDatabase database(kCollectionFixtureSql);
    if (!database.ok()) {
        QFAIL("could not build the collection fixture");
    }
    SqliteConnection& connection = database.connection();
    const QString none;

    // An empty sort keeps the curated playlist order.
    QCOMPARE(playlistTitles(connection, none, none),
             (QStringList{QStringLiteral("Marigold"), QStringLiteral("Zephyr"), QStringLiteral("Anchor")}));
    // An unrecognised sort falls back to the curated order.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Custom"), none),
             (QStringList{QStringLiteral("Marigold"), QStringLiteral("Zephyr"), QStringLiteral("Anchor")}));
    // Title sorts the collection alphabetically.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Title"), none),
             (QStringList{QStringLiteral("Anchor"), QStringLiteral("Marigold"), QStringLiteral("Zephyr")}));
    // Artist sorts by artist and then title.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Artist"), none),
             (QStringList{QStringLiteral("Anchor"), QStringLiteral("Zephyr"), QStringLiteral("Marigold")}));
    // Duration puts the longest track first.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Duration"), none),
             (QStringList{QStringLiteral("Zephyr"), QStringLiteral("Marigold"), QStringLiteral("Anchor")}));
    // DateAdded puts the newest track first.
    QCOMPARE(playlistTitles(connection, QStringLiteral("DateAdded"), none),
             (QStringList{QStringLiteral("Zephyr"), QStringLiteral("Marigold"), QStringLiteral("Anchor")}));

    // Find matches the album column, case-insensitively.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Title"), QStringLiteral("night")),
             (QStringList{QStringLiteral("Anchor"), QStringLiteral("Zephyr")}));
    // Find matches the artist column.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Title"), QStringLiteral("ada")),
             (QStringList{QStringLiteral("Anchor"), QStringLiteral("Zephyr")}));
    // Find matches a partial title.
    QCOMPARE(playlistTitles(connection, QStringLiteral("Title"), QStringLiteral("marig")),
             (QStringList{QStringLiteral("Marigold")}));
    // An inactive member stays out of the results.
    QVERIFY(playlistTitles(connection, QStringLiteral("Title"), QStringLiteral("dormant")).isEmpty());

    // Empty find text appends no clause at all.
    QVERIFY(grouping::CollectionSearchClause(none, 3).empty());
}

void LibraryGroupingSqlTest::atomicBulkPlaylistInsertion()
{
    const QString target = QStringLiteral("target");
    {
        MemoryDatabase database(kBulkFixtureSql);
        if (!database.ok()) {
            QFAIL("could not build the bulk playlist fixture");
        }
        // A valid bulk insertion commits and adds every track.
        QVERIFY(addTracksToPlaylistTransactional(database.connection(), target, {1, 2}).isOk());
        QCOMPARE(database.scalar("SELECT COUNT(*) FROM PlaylistTracks;"), 2);
        // New members receive contiguous append order.
        QCOMPARE(database.scalar("SELECT SUM(TrackOrder) FROM PlaylistTracks;"), 3);
    }
    {
        MemoryDatabase database(kBulkFixtureSql);
        if (!database.ok()) {
            QFAIL("could not build the bulk playlist fixture");
        }
        // Duplicates are accepted idempotently, without duplicate members or gaps.
        QVERIFY(addTracksToPlaylistTransactional(database.connection(), target, {1, 1, 2, 2}).isOk());
        QCOMPARE(database.scalar("SELECT COUNT(*) FROM PlaylistTracks;"), 2);
        QCOMPARE(database.scalar("SELECT MAX(TrackOrder) FROM PlaylistTracks;"), 2);
    }
    {
        MemoryDatabase database(kBulkFixtureSql);
        if (!database.ok()) {
            QFAIL("could not build the bulk playlist fixture");
        }
        // A missing playlist rejects the whole operation.
        QVERIFY(!addTracksToPlaylistTransactional(database.connection(), QStringLiteral("missing"), {1}).isOk());
        // An invalid track id fails before mutation.
        QVERIFY(!addTracksToPlaylistTransactional(database.connection(), target, {0, 1}).isOk());
        // Invalid targets leave the playlist untouched.
        QCOMPARE(database.scalar("SELECT COUNT(*) FROM PlaylistTracks;"), 0);
    }
    {
        MemoryDatabase database(kBulkFixtureSql);
        if (!database.ok()) {
            QFAIL("could not build the bulk playlist fixture");
        }
        // A missing track fails the bulk insertion and rolls back earlier members.
        QVERIFY(!addTracksToPlaylistTransactional(database.connection(), target, {1, 99}).isOk());
        QCOMPARE(database.scalar("SELECT COUNT(*) FROM PlaylistTracks;"), 0);
    }
}

QTEST_GUILESS_MAIN(LibraryGroupingSqlTest)

#include "tst_library_grouping_sql.moc"
