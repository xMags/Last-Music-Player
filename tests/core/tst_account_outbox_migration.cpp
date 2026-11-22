#include <QList>
#include <QObject>
#include <QString>
#include <QTest>

#include "TempLibrary.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountSchema.h"
#include "core/db/SqliteConnection.h"

#include <cstddef>
#include <optional>

using lmp::core::db::AccountOutboxRepository;
using lmp::core::db::PlaybackEventRecord;
using lmp::core::db::SqliteConnection;

namespace schema = lmp::core::db::account_schema;

namespace {

const QString kOwner = QStringLiteral("owner-a");

} // namespace

class AccountOutboxMigrationTest : public QObject {
    Q_OBJECT

private slots:
    void migrationPreservesQueuedBacklog();
};

// The database part of the earlier outbox migration test: tables from before
// the retry columns keep every queued row, and the rows become due.
void AccountOutboxMigrationTest::migrationPreservesQueuedBacklog()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(library.connection()
                .exec("DROP VIEW EffectiveTracks; DROP TABLE PlaybackEvents; DROP TABLE PendingLikes;"
                      "DROP TABLE AccountPlaylistTracks;"
                      "DROP TABLE AccountTracks;"
                      "CREATE TABLE PlaybackEvents ("
                      "EventId TEXT PRIMARY KEY, AccountId TEXT NOT NULL, RemoteId TEXT NOT NULL, TrackJson TEXT NOT NULL, "
                      "PlayedAtUtc TEXT NOT NULL, PositionSeconds REAL DEFAULT 0, AttemptCount INTEGER DEFAULT 0, LastAttemptAtUtc TEXT);"
                      "CREATE TABLE PendingLikes ("
                      "AccountId TEXT NOT NULL, RemoteId TEXT NOT NULL, DesiredState INTEGER NOT NULL, TrackJson TEXT NOT NULL, "
                      "UpdatedAtUtc TEXT NOT NULL, AttemptCount INTEGER DEFAULT 0, LastAttemptAtUtc TEXT, "
                      "PRIMARY KEY (AccountId, RemoteId));"
                      "CREATE TABLE AccountTracks ("
                      "AccountId TEXT NOT NULL, RemoteId TEXT NOT NULL, SourceUrl TEXT, Title TEXT, "
                      "PRIMARY KEY (AccountId, RemoteId));"
                      "INSERT INTO AccountProfiles (AccountId) VALUES ('owner-a');"
                      "INSERT INTO AccountTracks (AccountId,RemoteId,SourceUrl,Title) VALUES "
                      "('owner-a','existing-track','https://music.example/existing','Existing track');"
                      "WITH RECURSIVE sequence(value) AS ("
                      "SELECT 1 UNION ALL SELECT value+1 FROM sequence WHERE value<208) "
                      "INSERT INTO PlaybackEvents (EventId,AccountId,RemoteId,TrackJson,PlayedAtUtc) "
                      "SELECT printf('event-%03d',value),'owner-a',printf('track-%03d',value),"
                      "printf('{\"legacy\":%d}',value),'2026-08-01T00:00:00Z' FROM sequence;"
                      "INSERT INTO PendingLikes (AccountId,RemoteId,DesiredState,TrackJson,UpdatedAtUtc) "
                      "VALUES ('owner-a','liked-a',1,'{\"legacy\":true}','2026-08-01T00:00:00Z');")
                .isOk());

    SqliteConnection& connection = library.connection();
    for (const char* sql : {schema::CreateTablesSql, schema::AddPlaybackNextAttemptSql,
                            schema::AddPlaybackFailureCategorySql, schema::AddPlaybackBlockedSql,
                            schema::AddLikeNextAttemptSql, schema::AddLikeFailureCategorySql, schema::AddLikeBlockedSql,
                            schema::AddAccountTrackProviderSql, schema::AddAccountTrackCatalogIdSql,
                            schema::AddAccountTrackAlbumCatalogIdSql, schema::AddAccountTrackArtistCatalogIdSql}) {
        QVERIFY2(connection.exec(sql).isOk(), qPrintable(connection.lastError()));
    }

    AccountOutboxRepository outbox(library.database());
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents;"), 208);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE TrackJson LIKE '{\"legacy\":%';"), 208);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE NextAttemptAtUnix=0 AND IsBlocked=0;"), 208);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes WHERE NextAttemptAtUnix=0 AND IsBlocked=0;"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='AccountPreferences';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountTracks WHERE RemoteId='existing-track' "
                            "AND COALESCE(RemoteProvider,'')='' AND COALESCE(RemoteCatalogId,'')='' "
                            "AND COALESCE(AlbumCatalogId,'')='' AND COALESCE(ArtistCatalogId,'')='';"),
             1);

    // Read back through the repository: every row is due, payloads intact.
    const QList<PlaybackEventRecord> due = outbox.loadPendingPlaybackEvents(kOwner, 500);
    QCOMPARE(due.size(), 208);
    QCOMPARE(due.at(0).trackJson, QStringLiteral("{\"legacy\":1}"));
    QVERIFY(due.at(0).track.has_value());
    QCOMPARE(due.at(0).track->sourceKind, QStringLiteral("remote"));
    QCOMPARE(outbox.loadPendingLikes(kOwner).size(), 1);
    QCOMPARE(outbox.recoverablePendingChangeCount(kOwner), std::optional<std::size_t>(209));
}

QTEST_GUILESS_MAIN(AccountOutboxMigrationTest)
#include "tst_account_outbox_migration.moc"
