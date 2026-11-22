#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "core/app/AppPaths.h"
#include "core/db/Database.h"
#include "core/db/Schema.h"

using lmp::core::AppPaths;
using lmp::core::db::Database;
using lmp::core::db::Schema;
using lmp::core::db::SqliteConnection;
using lmp::core::db::SqliteStatement;

namespace {

QStringList column(SqliteConnection& connection, const char* sql)
{
    QStringList values;
    auto statement = connection.prepare(sql);
    if (!statement) {
        return {QStringLiteral("<error> ") + statement.failure().detail};
    }
    while (statement.value().step().valueOr(false)) {
        values << statement.value().columnText(0);
    }
    return values;
}

QString single(SqliteConnection& connection, const char* sql)
{
    const QStringList values = column(connection, sql);
    return values.isEmpty() ? QString() : values.first();
}

QStringList sorted(QStringList list)
{
    list.sort();
    return list;
}

const QStringList kTables{
    QStringLiteral("AccountPlaylistTracks"), QStringLiteral("AccountPlaylists"),  QStringLiteral("AccountPreferences"),
    QStringLiteral("AccountProfiles"),       QStringLiteral("AccountSyncState"),  QStringLiteral("AccountTracks"),
    QStringLiteral("ActiveAccountContext"),  QStringLiteral("AlbumTracks"),       QStringLiteral("Albums"),
    QStringLiteral("CatalogDiscoveryCache"), QStringLiteral("FollowedArtists"),   QStringLiteral("PendingLikes"),
    QStringLiteral("PlaybackEvents"),        QStringLiteral("PlaylistTracks"),    QStringLiteral("Playlists"),
    QStringLiteral("Tracks"),
};

const QStringList kIndexes{
    QStringLiteral("IX_AccountPlaylistTracks_Order"), QStringLiteral("IX_AccountPlaylistTracks_Track"),
    QStringLiteral("IX_AccountPreferences_Retry"),    QStringLiteral("IX_AccountTracks_History"),
    QStringLiteral("IX_AccountTracks_Liked"),         QStringLiteral("IX_AlbumTracks_Order"),
    QStringLiteral("IX_AlbumTracks_TrackId"),         QStringLiteral("IX_PendingLikes_Account"),
    QStringLiteral("IX_PendingLikes_Retry"),          QStringLiteral("IX_PlaybackEvents_Account"),
    QStringLiteral("IX_PlaybackEvents_Retry"),        QStringLiteral("IX_PlaylistTracks_Order"),
    QStringLiteral("IX_PlaylistTracks_TrackId"),      QStringLiteral("IX_Tracks_Album"),
    QStringLiteral("IX_Tracks_Artist"),               QStringLiteral("IX_Tracks_DateAdded"),
    QStringLiteral("IX_Tracks_Genre"),                QStringLiteral("IX_Tracks_RemoteId"),
    QStringLiteral("IX_Tracks_SourceKey"),            QStringLiteral("IX_Tracks_SourceKind_Active"),
};

// Everything the schema step leaves in sqlite_master plus the row counts of
// each table: two runs that change nothing give the same text.
QString snapshot(SqliteConnection& connection)
{
    QStringList parts =
        column(connection, "SELECT type || ' ' || name || ' ' || COALESCE(sql,'') FROM sqlite_master ORDER BY name;");
    for (const QString& table : kTables) {
        const QByteArray sql = "SELECT COUNT(*) FROM " + table.toUtf8() + ";";
        parts << table + QLatin1Char('=') + single(connection, sql.constData());
    }
    parts << single(connection, "PRAGMA user_version;");
    return parts.join(QLatin1Char('\n'));
}

} // namespace

class SchemaTest : public QObject {
    Q_OBJECT

private slots:
    void freshDatabaseEndsAtVersion14();
    void runningTwiceChangesNothing();
    void versionOneDatabaseMigratesWithoutLosingRows();
    void newerVersionIsLeftAlone();
};

void SchemaTest::freshDatabaseEndsAtVersion14()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    const auto initialized = database.initialize();
    QVERIFY2(initialized.isOk(), qPrintable(initialized.isOk() ? QString() : initialized.failure().detail));
    SqliteConnection& connection = database.connection();
    QCOMPARE(Schema::userVersion(connection), 14);
    QCOMPARE(sorted(column(connection, "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%';")),
             sorted(kTables));
    QCOMPARE(sorted(column(connection, "SELECT name FROM sqlite_master WHERE type='index' AND name LIKE 'IX_%';")),
             sorted(kIndexes));
    QCOMPARE(column(connection, "SELECT name FROM sqlite_master WHERE type='view';"),
             QStringList{QStringLiteral("EffectiveTracks")});
    QCOMPARE(single(connection, "SELECT RemoteMode || '|' || COALESCE(AccountId,'') FROM ActiveAccountContext;"),
             QStringLiteral("LocalOnly|"));
}

void SchemaTest::runningTwiceChangesNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.initialize().isOk());
    SqliteConnection& connection = database.connection();
    QVERIFY(connection
                .exec("INSERT INTO Tracks (SourceKey, SourceKind, FilePath, Title) VALUES ('local|c:\\a.mp3','local','C:\\a.mp3','A');"
                      "INSERT INTO Playlists (PlaylistKey, Title) VALUES ('manual|1','Mine');"
                      "INSERT INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) VALUES (1, 1, 0);")
                .isOk());
    const QString before = snapshot(connection);
    QVERIFY(Schema::ensure(connection).isOk());
    QCOMPARE(snapshot(connection), before);
}

void SchemaTest::versionOneDatabaseMigratesWithoutLosingRows()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.open().isOk());
    SqliteConnection& connection = database.connection();
    // The oldest shape the migration handles: Tracks with a unique FilePath
    // and few columns (plus the version 3 ReplayGainDb column), Playlists with
    // only a key and a title, and an imported (non-manual) album collection.
    QVERIFY(connection
                .exec("CREATE TABLE Tracks (Id INTEGER PRIMARY KEY AUTOINCREMENT, FilePath TEXT UNIQUE, Title TEXT, Artist TEXT, ReplayGainDb REAL);"
                      "CREATE TABLE Albums (Id INTEGER PRIMARY KEY AUTOINCREMENT, AlbumKey TEXT UNIQUE, Title TEXT, Artist TEXT, ArtworkUrl TEXT, SourceKind TEXT DEFAULT 'manual', SourceUrl TEXT, SourceLabel TEXT, CreatedAt INTEGER DEFAULT 0, UpdatedAt INTEGER DEFAULT 0);"
                      "CREATE TABLE AlbumTracks (AlbumId INTEGER NOT NULL, TrackId INTEGER NOT NULL, TrackOrder INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (AlbumId, TrackId));"
                      "CREATE TABLE Playlists (Id INTEGER PRIMARY KEY AUTOINCREMENT, PlaylistKey TEXT UNIQUE, Title TEXT);"
                      "CREATE TABLE PlaylistTracks (PlaylistId INTEGER NOT NULL, TrackId INTEGER NOT NULL, TrackOrder INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (PlaylistId, TrackId));"
                      "INSERT INTO Tracks (FilePath, Title, Artist, ReplayGainDb) VALUES ('D:\\Music\\One.MP3','One','Band',0.5);"
                      "INSERT INTO Tracks (FilePath, Title, Artist) VALUES ('D:\\Music\\Two.flac','Two','Band');"
                      "INSERT INTO Tracks (FilePath, Title, Artist) VALUES ('D:\\Music\\Three.flac','Three','Other');"
                      "INSERT INTO Playlists (PlaylistKey, Title) VALUES ('manual|100','Road trip');"
                      "INSERT INTO PlaylistTracks VALUES (1, 2, 0);"
                      "INSERT INTO PlaylistTracks VALUES (1, 1, 1);"
                      "INSERT INTO Albums (AlbumKey, Title, Artist, SourceKind, SourceUrl, SourceLabel) VALUES ('imported|7','Mix','Someone','Server','https://music.example.test/list/7','Server');"
                      "INSERT INTO Albums (AlbumKey, Title, Artist) VALUES ('manual|5','Kept album','Me');"
                      "INSERT INTO AlbumTracks VALUES (1, 3, 0);"
                      "INSERT INTO AlbumTracks VALUES (1, 1, 1);"
                      "INSERT INTO AlbumTracks VALUES (2, 2, 0);"
                      "PRAGMA user_version=1;")
                .isOk());

    const auto ensured = Schema::ensure(connection);
    QVERIFY2(ensured.isOk(), qPrintable(ensured.isOk() ? QString() : ensured.failure().detail));
    QCOMPARE(Schema::userVersion(connection), 14);

    // Every song is still there, keyed by its lower-cased path.
    QCOMPARE(column(connection, "SELECT Id || ':' || Title || ':' || SourceKey || ':' || SourceKind FROM Tracks ORDER BY Id;"),
             (QStringList{QStringLiteral("1:One:local|d:\\music\\one.mp3:local"),
                          QStringLiteral("2:Two:local|d:\\music\\two.flac:local"),
                          QStringLiteral("3:Three:local|d:\\music\\three.flac:local")}));
    // FilePath is no longer unique, and ReplayGainDb is gone.
    const QString tracksSql = single(connection, "SELECT sql FROM sqlite_master WHERE name='Tracks';");
    QVERIFY(!tracksSql.contains(QStringLiteral("FilePath TEXT UNIQUE")));
    QVERIFY(!tracksSql.contains(QStringLiteral("ReplayGainDb")));
    QVERIFY(tracksSql.contains(QStringLiteral("ArtistCatalogId")));

    // The manual playlist keeps its songs in order.
    QCOMPARE(column(connection, "SELECT TrackId FROM PlaylistTracks WHERE PlaylistId=1 ORDER BY TrackOrder;"),
             (QStringList{QStringLiteral("2"), QStringLiteral("1")}));
    // The imported album collection became a playlist with its songs; the
    // manual album stays an album.
    QCOMPARE(single(connection, "SELECT Title || '|' || Description || '|' || SourceKind || '|' || Provider || '|' || SourceLabel "
                                "FROM Playlists WHERE PlaylistKey='import-playlist|server|https://music.example.test/list/7';"),
             QStringLiteral("Mix|Someone|manual|Server|Music API"));
    QCOMPARE(column(connection, "SELECT pt.TrackId FROM PlaylistTracks pt JOIN Playlists p ON p.Id=pt.PlaylistId "
                                "WHERE p.PlaylistKey LIKE 'import-playlist|%' ORDER BY pt.TrackOrder;"),
             (QStringList{QStringLiteral("3"), QStringLiteral("1")}));
    QCOMPARE(column(connection, "SELECT AlbumKey FROM Albums;"), QStringList{QStringLiteral("manual|5")});
    QCOMPARE(column(connection, "SELECT TrackId FROM AlbumTracks;"), QStringList{QStringLiteral("2")});

    // A second run is a no-op.
    const QString before = snapshot(connection);
    QVERIFY(Schema::ensure(connection).isOk());
    QCOMPARE(snapshot(connection), before);
}

void SchemaTest::newerVersionIsLeftAlone()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.initialize().isOk());
    QVERIFY(database.connection().exec("PRAGMA user_version=15;").isOk());
    QVERIFY(Schema::ensure(database.connection()).isOk());
    QCOMPARE(Schema::userVersion(database.connection()), 15);
}

QTEST_GUILESS_MAIN(SchemaTest)
#include "tst_schema.moc"
