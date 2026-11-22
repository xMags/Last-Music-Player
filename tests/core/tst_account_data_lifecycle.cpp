#include <QObject>
#include <QString>
#include <QTest>

#include "TempLibrary.h"
#include "core/account/AccountDataLifecycleStorage.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRecords.h"
#include "core/db/AccountRepository.h"
#include "core/db/AccountSchema.h"
#include "core/db/LibraryQueries.h"

#include <optional>

using lmp::core::FailureKind;
using lmp::core::account::AccountDataLifecycleStorage;
using lmp::core::db::AccountDataClearMode;
using lmp::core::db::AccountOutboxRepository;
using lmp::core::db::AccountRepository;
using lmp::core::db::AccountSyncState;
using lmp::core::db::LibraryQueries;

namespace schema = lmp::core::db::account_schema;

namespace {

// Two accounts with a row in every account table, a local song, a song from
// an API key server, a stale pre-account row and a local playlist.
bool seedTwoOwners(TempLibrary& library)
{
    return library.connection()
        .exec(
            "INSERT INTO Tracks (SourceKey,SourceKind,Provider,SourceUrl,FilePath,Title,IsActive,RemoteId) VALUES "
            "('local|one','local','','','C:/Music/local.flac','Local song',1,''),"
            "('remote|api|a','remote','api','https://music.example/a','https://stream.example/a?media_token=secret','API duplicate',1,'remote-a'),"
            "('account|owner-a|old','remote','account','https://music.example/old','','Leaked account row',1,'old');"
            "UPDATE Tracks SET ArtworkUrl='https://images.example/a?signature=secret' WHERE SourceKey='remote|api|a';"
            "INSERT INTO Playlists (PlaylistKey,Title,ArtworkUrl) VALUES ('manual|1','Local list','local-playlist');"
            "INSERT INTO AccountProfiles (AccountId,DisplayName) VALUES ('owner-a','A'),('owner-b','B');"
            "INSERT INTO AccountTracks (AccountId,RemoteId,SourceUrl,Title,IsLiked,PlayCount,RemoteProvider,RemoteCatalogId,AlbumCatalogId,ArtistCatalogId,DateAddedText) VALUES "
            "('owner-a','remote-a','https://music.example/a','Account A song',1,4,'source-a','catalog-a','album-a','artist-a','2026-08-20T10:00:00Z'),"
            "('owner-a','remote-new','https://music.example/new','Newer account song',0,0,'source-a','catalog-new','album-a','artist-a','2026-08-22T10:00:00Z'),"
            "('owner-b','remote-b','https://music.example/b','Account B song',0,2,'source-b','catalog-b','album-b','artist-b','2026-08-21T10:00:00Z');"
            "INSERT INTO PlaybackEvents (EventId,AccountId,RemoteId,TrackJson,PlayedAtUtc) VALUES "
            "('event-a','owner-a','remote-a','{}','2026-08-01T00:00:00Z'),"
            "('event-b','owner-b','remote-b','{}','2026-08-01T00:00:01Z');"
            "INSERT INTO PendingLikes (AccountId,RemoteId,DesiredState,TrackJson,UpdatedAtUtc) VALUES "
            "('owner-a','shared-track',1,'{}','2026-08-01T00:00:00Z'),"
            "('owner-b','shared-track',0,'{}','2026-08-01T00:00:01Z');"
            "INSERT INTO AccountSyncState (AccountId,LegacyHistoryImportState) VALUES "
            "('owner-a','accepted'),('owner-b','declined');"
            "INSERT INTO AccountPlaylists (AccountId,PlaylistId,Name) VALUES "
            "('owner-a','playlist-a','A list'),('owner-b','playlist-b','B list');"
            "INSERT INTO AccountPlaylistTracks (AccountId,PlaylistId,RemoteId,TrackOrder) VALUES "
            "('owner-a','playlist-a','remote-a',0),('owner-b','playlist-b','remote-b',0);"
            "INSERT INTO AccountPreferences (AccountId,PreferenceKey,ValueJson,Pending) VALUES "
            "('owner-a','theme','\"dark\"',1),('owner-b','theme','\"light\"',0);"
            "INSERT INTO CatalogDiscoveryCache (AccountId,Storefront,Payload) VALUES "
            "('owner-a','in','{}'),('owner-b','us','{}');")
        .isOk();
}

} // namespace

class AccountDataLifecycleTest : public QObject {
    Q_OBJECT

private slots:
    void ownerScopedSchemaIsolatesAccounts();
    void effectiveTracksFollowTheActiveContext();
    void clearLeavesOtherAccountsAndActiveContext();
    void clearCanRestoreTheActiveContext();
    void clearAllRemovesEveryAccount();
    void lifecycleStorageCountsAndRemoves();
    void syncStateRoundTrips();
    void discoveryCacheIsPerAccountAndRegion();
};

// The database part of the earlier owner-scoped schema test.
void AccountDataLifecycleTest::ownerScopedSchemaIsolatesAccounts()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    AccountRepository accounts(library.database());
    AccountOutboxRepository outbox(library.database());

    QVERIFY(library.connection().exec(schema::RemoveLegacyGenericAccountTracksSql).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE Provider='account';"), 0);
    QVERIFY(library.connection().exec(schema::RemoveTransientRemoteUrlsSql).isOk());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE SourceKey='remote|api|a' AND FilePath='' AND ArtworkUrl='';"), 1);

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account A song';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE RemoteProvider='source-a' AND RemoteCatalogId='catalog-a' "
                            "AND AlbumCatalogId='album-a' AND ArtistCatalogId='artist-a';"),
             1);
    QCOMPARE(library.number("SELECT (SELECT Title FROM EffectiveTracks WHERE Provider='account' "
                            "ORDER BY DateAddedSortKey DESC LIMIT 1)='Newer account song';"),
             1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account B song';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='API duplicate';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND FilePath<>'';"), 0);

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-b")));
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account A song';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account' AND Title='Account B song';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-a';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-b';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes WHERE AccountId='owner-a' AND DesiredState=1;"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes WHERE AccountId='owner-b' AND DesiredState=0;"), 1);
    QCOMPARE(accounts.loadAccountSyncState(QStringLiteral("owner-a")).legacyHistoryImportState, QStringLiteral("accepted"));
    QCOMPARE(accounts.loadAccountSyncState(QStringLiteral("owner-b")).legacyHistoryImportState, QStringLiteral("declined"));

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("LocalOnly")));
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE SourceKind='remote';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='Local song';"), 1);

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("ApiKey")));
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Title='API duplicate';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM EffectiveTracks WHERE Provider='account';"), 0);

    QVERIFY(outbox.acknowledgePlaybackEvents(QStringLiteral("owner-a"), {QStringLiteral("event-a")}));
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-a';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-b';"), 1);

    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-a")));
    for (const char* table : {"AccountTracks", "AccountSyncState", "AccountPlaylists", "AccountPreferences",
                              "CatalogDiscoveryCache"}) {
        const QByteArray ownerA = QByteArray("SELECT COUNT(*) FROM ") + table + " WHERE AccountId='owner-a';";
        const QByteArray ownerB = QByteArray("SELECT COUNT(*) FROM ") + table + " WHERE AccountId='owner-b';";
        QCOMPARE(library.number(ownerA.constData()), 0);
        QCOMPARE(library.number(ownerB.constData()), 1);
    }
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountPlaylistTracks WHERE AccountId='owner-a';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE SourceKey='local|one';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Playlists WHERE ArtworkUrl='local-playlist';"), 1);
}

void AccountDataLifecycleTest::effectiveTracksFollowTheActiveContext()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    QVERIFY(library.connection().exec(schema::RemoveLegacyGenericAccountTracksSql).isOk());
    AccountRepository accounts(library.database());
    LibraryQueries queries(library.database());

    const auto titles = [&queries]() {
        QStringList result;
        for (const auto& track : queries.loadTracks()) {
            result << track.title;
        }
        result.sort();
        return result;
    };
    QCOMPARE(titles(), QStringList{QStringLiteral("Local song")});

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QCOMPARE(titles(),
             (QStringList{QStringLiteral("Account A song"), QStringLiteral("Local song"), QStringLiteral("Newer account song")}));
    for (const auto& track : queries.loadTracks()) {
        if (track.origin == QStringLiteral("account")) {
            QVERIFY(track.rowId < 0);
        }
    }

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("ApiKey")));
    QCOMPARE(titles(), (QStringList{QStringLiteral("API duplicate"), QStringLiteral("Local song")}));

    // Clearing the active account drops its songs from the view at once.
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-b")));
    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-b")));
    QCOMPARE(accounts.activeAccountId(), QString());
    QCOMPARE(titles(), QStringList{QStringLiteral("Local song")});
}

void AccountDataLifecycleTest::clearLeavesOtherAccountsAndActiveContext()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-b")));

    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-a")));
    QCOMPARE(accounts.activeAccountId(), QStringLiteral("owner-b"));
    QCOMPARE(library.text("SELECT group_concat(AccountId) FROM AccountProfiles;"), QStringLiteral("owner-b"));
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents;"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PendingLikes;"), 1);

    // Clearing an unknown account succeeds and changes nothing.
    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-unknown")));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles;"), 1);
    const auto empty = accounts.clearAccountData(QString());
    QVERIFY(!empty);
    QCOMPARE(empty.failure().kind, FailureKind::NotFound);
}

void AccountDataLifecycleTest::clearCanRestoreTheActiveContext()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("LocalOnly")));

    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-a"), AccountDataClearMode::RestoreActiveContext));
    QCOMPARE(accounts.activeAccountId(), QStringLiteral("owner-a"));
    QCOMPARE(library.text("SELECT COALESCE(DisplayName,'<empty>') FROM AccountProfiles WHERE AccountId='owner-a';"),
             QStringLiteral("<empty>"));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountTracks WHERE AccountId='owner-a';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM PlaybackEvents WHERE AccountId='owner-a';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountTracks WHERE AccountId='owner-b';"), 1);
}

void AccountDataLifecycleTest::clearAllRemovesEveryAccount()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    AccountRepository accounts(library.database());
    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));

    QVERIFY(accounts.clearAllAccountData());
    QCOMPARE(library.text("SELECT RemoteMode || '|' || AccountId FROM ActiveAccountContext;"), QStringLiteral("LocalOnly|"));
    for (const char* table : {"AccountProfiles", "AccountTracks", "AccountPlaylists", "AccountPlaylistTracks",
                              "PlaybackEvents", "PendingLikes", "AccountSyncState", "AccountPreferences",
                              "CatalogDiscoveryCache"}) {
        const QByteArray sql = QByteArray("SELECT COUNT(*) FROM ") + table + ";";
        QCOMPARE(library.number(sql.constData()), 0);
    }
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE SourceKey='local|one';"), 1);
}

// The storage side of the earlier sign-out tests: blocked rows do not hold a
// sign-out back, and removal keeps every other account and local data.
void AccountDataLifecycleTest::lifecycleStorageCountsAndRemoves()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(seedTwoOwners(library));
    AccountRepository accounts(library.database());
    AccountDataLifecycleStorage& storage = accounts;

    QVERIFY(storage.isAccountDataStoreReady());
    QCOMPARE(storage.recoverablePendingChangeCount(QStringLiteral("owner-a")), std::optional<std::size_t>(3));
    QVERIFY(library.connection().exec("UPDATE PlaybackEvents SET IsBlocked=1 WHERE AccountId='owner-a';"
                                      "UPDATE PendingLikes SET IsBlocked=1 WHERE AccountId='owner-a';")
                .isOk());
    QCOMPARE(storage.recoverablePendingChangeCount(QStringLiteral("owner-a")), std::optional<std::size_t>(1));
    QCOMPARE(storage.recoverablePendingChangeCount(QStringLiteral("owner-unknown")), std::optional<std::size_t>(0));
    QVERIFY(!storage.recoverablePendingChangeCount(QString()).has_value());

    QVERIFY(accounts.setRemoteLibraryContext(QStringLiteral("Account"), QStringLiteral("owner-a")));
    QVERIFY(storage.removeSignedOutAccountData(QStringLiteral("owner-a")));
    QCOMPARE(accounts.activeAccountId(), QString());
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles WHERE AccountId='owner-a';"), 0);
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles WHERE AccountId='owner-b';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE SourceKey='local|one';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Tracks WHERE SourceKey='remote|api|a';"), 1);
    QCOMPARE(library.number("SELECT COUNT(*) FROM Playlists;"), 1);

    library.database().close();
    QVERIFY(!storage.isAccountDataStoreReady());
    QVERIFY(!storage.recoverablePendingChangeCount(QStringLiteral("owner-b")).has_value());
}

void AccountDataLifecycleTest::syncStateRoundTrips()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());

    const AccountSyncState missing = accounts.loadAccountSyncState(QStringLiteral("owner-a"));
    QCOMPARE(missing.accountId, QStringLiteral("owner-a"));
    QVERIFY(missing.lastSuccessfulSyncUtc.isEmpty() && missing.legacyHistoryImportState.isEmpty());

    AccountSyncState state;
    state.accountId = QStringLiteral("owner-a");
    state.lastSuccessfulSyncUtc = QStringLiteral("2026-08-21T10:00:00Z");
    state.lastSafeErrorCode = QStringLiteral("offline");
    state.legacyHistoryImportState = QStringLiteral("declined");
    QVERIFY(accounts.saveAccountSyncState(state));
    QCOMPARE(library.number("SELECT COUNT(*) FROM AccountProfiles WHERE AccountId='owner-a';"), 1);

    const AccountSyncState loaded = accounts.loadAccountSyncState(QStringLiteral("owner-a"));
    QCOMPARE(loaded.lastSuccessfulSyncUtc, state.lastSuccessfulSyncUtc);
    QCOMPARE(loaded.lastSafeErrorCode, state.lastSafeErrorCode);
    QCOMPARE(loaded.legacyHistoryImportState, state.legacyHistoryImportState);

    state.lastSafeErrorCode.clear();
    QVERIFY(accounts.saveAccountSyncState(state));
    QCOMPARE(accounts.loadAccountSyncState(QStringLiteral("owner-a")).lastSafeErrorCode, QString());

    AccountSyncState anonymous;
    const auto refused = accounts.saveAccountSyncState(anonymous);
    QVERIFY(!refused);
    QCOMPARE(refused.failure().kind, FailureKind::NotFound);
    QCOMPARE(accounts.loadAccountSyncState(QString()).accountId, QString());
}

void AccountDataLifecycleTest::discoveryCacheIsPerAccountAndRegion()
{
    TempLibrary library;
    QVERIFY(library.ok());
    AccountRepository accounts(library.database());

    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("in")), QString());
    QVERIFY(accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("in"), QStringLiteral("{\"a\":1}")));
    QVERIFY(accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("us"), QStringLiteral("{\"b\":2}")));
    QVERIFY(accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("in"), QStringLiteral("{\"c\":3}")));
    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("in")), QStringLiteral("{\"c\":3}"));
    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("us")), QStringLiteral("{\"b\":2}"));
    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-b"), QStringLiteral("in")), QString());
    QVERIFY(!library.text("SELECT FetchedAtUtc FROM CatalogDiscoveryCache WHERE Storefront='in';").isEmpty());

    QVERIFY(!accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("in"), QString()));
    QVERIFY(!accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QString(), QStringLiteral("{}")));
    const QString atLimit(4 * 1024 * 1024, QLatin1Char('x'));
    QVERIFY(accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("gb"), atLimit));
    const auto oversized =
        accounts.saveCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("gb"), atLimit + QLatin1Char('x'));
    QVERIFY(!oversized);
    QCOMPARE(oversized.failure().kind, FailureKind::NotFound);
    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("gb")).size(), atLimit.size());

    QVERIFY(accounts.clearAccountData(QStringLiteral("owner-a")));
    QCOMPARE(accounts.loadCatalogDiscoveryPayload(QStringLiteral("owner-a"), QStringLiteral("us")), QString());
}

QTEST_GUILESS_MAIN(AccountDataLifecycleTest)
#include "tst_account_data_lifecycle.moc"
