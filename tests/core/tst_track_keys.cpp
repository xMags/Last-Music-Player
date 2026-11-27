#include <QObject>
#include <QString>
#include <QTest>

#include "core/model/TrackKeys.h"

namespace keys = lmp::core::track_keys;
using lmp::core::mode::SourceMode;
using lmp::core::Track;

namespace {

Track localSong()
{
    Track track;
    track.title = QStringLiteral("Song");
    track.artist = QStringLiteral("Artist");
    track.filePath = QStringLiteral("C:/Music/Song.MP3");
    track.sourceKind = QStringLiteral("local");
    return track;
}

Track remoteSong()
{
    Track track;
    track.title = QStringLiteral("Song");
    track.sourceKind = QStringLiteral("Remote");
    track.origin = QStringLiteral("Source-A");
    track.sourceUrl = QStringLiteral("https://music.example.test/t/1");
    return track;
}

keys::RemoteScopeKeyParts scope(SourceMode mode)
{
    return {mode, 3, 7, QStringLiteral("p1"), QStringLiteral("owner-a")};
}

} // namespace

// Cache and download keys, with fixed expected text: they must match what
// earlier versions wrote.
class TrackKeysTest : public QObject {
    Q_OBJECT

private slots:
    void scopeKeys();
    void streamCacheKeys();
    void downloadKeys();
    void downloadKeysOutliveRunCounters();
    void earlierDownloadKeysStillMatch();
    void localOnlyFindsTheAccountAndProviderDownloads();
};

void TrackKeysTest::scopeKeys()
{
    QCOMPARE(keys::remoteScopeCacheKey(scope(SourceMode::LocalOnly)), QStringLiteral("LocalOnly:3"));
    QCOMPARE(keys::remoteScopeCacheKey(scope(SourceMode::ApiKey)), QStringLiteral("ApiKey:p1:3"));
    QCOMPARE(keys::remoteScopeCacheKey(scope(SourceMode::Account)), QStringLiteral("Account:3:7"));
    QCOMPARE(keys::remoteScopeCacheKey({SourceMode::ApiKey, 0, 0, QString()}), QStringLiteral("ApiKey::0"));
}

void TrackKeysTest::streamCacheKeys()
{
    QCOMPARE(keys::streamCacheKey(scope(SourceMode::ApiKey), remoteSong()),
             QStringLiteral("ApiKey:p1:3\nhttps://music.example.test/t/1"));
    QVERIFY(keys::streamCacheKey(scope(SourceMode::Account), remoteSong()).isEmpty());
    QVERIFY(keys::streamCacheKey(scope(SourceMode::ApiKey), localSong()).isEmpty());
}

void TrackKeysTest::downloadKeys()
{
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::LocalOnly), localSong()), QStringLiteral("local|c:/music/song.mp3"));
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), localSong()), QStringLiteral("local|c:/music/song.mp3"));
    QVERIFY(keys::downloadStableKey(scope(SourceMode::LocalOnly), remoteSong()).isEmpty());
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), remoteSong()),
             QStringLiteral("download|Account:owner-a\nremote|source-a|https://music.example.test/t/1"));
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::ApiKey), remoteSong()),
             QStringLiteral("download|ApiKey:p1\nremote|source-a|https://music.example.test/t/1"));

    // No path and no address: the history key stands in, as before.
    Track bare = remoteSong();
    bare.sourceUrl.clear();
    bare.artist = QStringLiteral("The  Artist!");
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), bare),
             QStringLiteral("download|Account:owner-a\nremote|song|the artist"));
    // A song with nothing at all still has the local history key.
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), Track()), QStringLiteral("local|||"));
}

void TrackKeysTest::downloadKeysOutliveRunCounters()
{
    // The same song in the same account, after the session was checked again
    // or the app started over: the counters moved, the download did not.
    keys::RemoteScopeKeyParts before = scope(SourceMode::Account);
    keys::RemoteScopeKeyParts after = before;
    after.generation += 5;
    after.accountGeneration += 2;
    QCOMPARE(keys::downloadStableKey(after, remoteSong()), keys::downloadStableKey(before, remoteSong()));
}

void TrackKeysTest::earlierDownloadKeysStillMatch()
{
    // No account known yet: no key to keep a download under.
    keys::RemoteScopeKeyParts unknown = scope(SourceMode::Account);
    unknown.ownerId.clear();
    QVERIFY(keys::downloadStableKey(unknown, remoteSong()).isEmpty());

    const QString song = QStringLiteral("\nremote|source-a|https://music.example.test/t/1");
    const QString earlierAccount = QStringLiteral("download|Account:3:7") + song;
    const QString todayAccount = QStringLiteral("download|Account:owner-a") + song;
    const QString earlierApi = QStringLiteral("download|ApiKey:p1:3") + song;
    // The same song matches across the two forms, within its mode.
    QCOMPARE(keys::downloadMatchKey(earlierAccount), keys::downloadMatchKey(todayAccount));
    QCOMPARE(keys::downloadMatchKey(earlierApi), keys::downloadMatchKey(QStringLiteral("download|ApiKey:p1") + song));
    QVERIFY(keys::downloadMatchKey(earlierApi) != keys::downloadMatchKey(todayAccount));
    QVERIFY(keys::downloadMatchKey(earlierApi)
            != keys::downloadMatchKey(QStringLiteral("download|ApiKey:p2") + song)); // another provider
    // A song on this PC is its own key.
    QCOMPARE(keys::downloadMatchKey(QStringLiteral("local|c:/music/song.mp3")), QStringLiteral("local|c:/music/song.mp3"));

    // Running: today's keys by account; earlier ones (no account named) by mode.
    const QString scopeA = QStringLiteral("download|Account:owner-a\n");
    QVERIFY(keys::downloadInScope(todayAccount, scopeA));
    QVERIFY(!keys::downloadInScope(QStringLiteral("download|Account:owner-b") + song, scopeA));
    QVERIFY(keys::downloadInScope(earlierAccount, scopeA));
    QVERIFY(!keys::downloadInScope(earlierApi, scopeA));
    QVERIFY(keys::downloadInScope(earlierApi, QStringLiteral("download|ApiKey:p1\n")));
    QVERIFY(!keys::downloadInScope(earlierApi, QStringLiteral("download|ApiKey:p2\n")));
    QVERIFY(!keys::downloadInScope(todayAccount, QString()));
}

void TrackKeysTest::localOnlyFindsTheAccountAndProviderDownloads()
{
    const QList<keys::RemoteScopeKeyParts> both = keys::localOnlyDownloadScopes(QStringLiteral("owner-a"), QStringLiteral("p1"));
    QCOMPARE(both.size(), 2);
    // The keys they make are the ones the songs were downloaded under.
    QCOMPARE(keys::downloadStableKey(both.at(0), remoteSong()),
             keys::downloadStableKey(scope(SourceMode::Account), remoteSong()));
    QCOMPARE(keys::downloadStableKey(both.at(1), remoteSong()),
             keys::downloadStableKey(scope(SourceMode::ApiKey), remoteSong()));

    QCOMPARE(keys::localOnlyDownloadScopes(QString(), QStringLiteral("p1")).size(), 1);
    QCOMPARE(keys::localOnlyDownloadScopes(QStringLiteral("owner-a"), QString()).size(), 1);
    QVERIFY(keys::localOnlyDownloadScopes(QString(), QString()).isEmpty());
}

QTEST_GUILESS_MAIN(TrackKeysTest)
#include "tst_track_keys.moc"
