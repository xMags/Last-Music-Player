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
    return {mode, 3, 7, QStringLiteral("p1")};
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
             QStringLiteral("download|Account:3:7\nremote|source-a|https://music.example.test/t/1"));
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::ApiKey), remoteSong()),
             QStringLiteral("download|ApiKey:p1:3\nremote|source-a|https://music.example.test/t/1"));

    // No path and no address: the history key stands in, as before.
    Track bare = remoteSong();
    bare.sourceUrl.clear();
    bare.artist = QStringLiteral("The  Artist!");
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), bare),
             QStringLiteral("download|Account:3:7\nremote|song|the artist"));
    // A song with nothing at all still has the local history key.
    QCOMPARE(keys::downloadStableKey(scope(SourceMode::Account), Track()), QStringLiteral("local|||"));
}

QTEST_GUILESS_MAIN(TrackKeysTest)
#include "tst_track_keys.moc"
