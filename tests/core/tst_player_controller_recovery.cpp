#include <QFile>
#include <QObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "PlayerFixture.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/TrackSourceResolver.h"

using lmp::core::PlayerController;
using lmp::core::Track;

namespace {

// The PlayCount of the song's row, or -1 when it has no row.
[[nodiscard]] qint64 playCount(PlayerFixture& fixture, const Track& track)
{
    const QByteArray query = QStringLiteral("SELECT PlayCount FROM Tracks WHERE SourceKey='%1';")
                                 .arg(lmp::core::catalogSourceKey(track))
                                 .toUtf8();
    return fixture.library.number(query.constData());
}

[[nodiscard]] qint64 lastPlayedOrder(PlayerFixture& fixture, const Track& track)
{
    const QByteArray query = QStringLiteral("SELECT LastPlayedOrder FROM Tracks WHERE SourceKey='%1';")
                                 .arg(lmp::core::catalogSourceKey(track))
                                 .toUtf8();
    return fixture.library.number(query.constData());
}

// Plays on for `milliseconds`, with a position report every 500 ms as the
// player sends them.
void playFor(PlayerFixture& fixture, int milliseconds, double& position)
{
    for (int elapsed = 0; elapsed < milliseconds; elapsed += 500) {
        fixture.clock.advance(500);
        position += 0.5;
        fixture.audio.position(position);
    }
}

} // namespace

// Broken songs, history and resolving rules of the player controller.
class PlayerControllerRecoveryTest : public QObject {
    Q_OBJECT

private slots:
    void unresolvableSongIsSkipped();
    void songThePlayerCannotReadIsSkipped();
    void threeBrokenSongsInARowStopPlayback();
    void aSongThatStartsResetsTheCount();
    void historyIsRecordedAfterEightSecondsOfPlay();
    void historyCountsOnlyTimeSpentPlaying();
    void historyIsRecordedOncePerPlay();
    void supersededResolveIsIgnored();
    void localFileResolverFindsFilesOnly();
    void catalogSourceKeyMatchesStoredRows();
};

void PlayerControllerRecoveryTest::unresolvableSongIsSkipped()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    fixture.resolver.broken.insert(fixture.song(QStringLiteral("b")).filePath);
    QSignalSpy failedSpy(player.get(), &PlayerController::trackFailed);

    player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b", "c"}));
    settle();
    fixture.audio.start();
    player->next();
    settle();
    QCOMPARE(player->state(), PlayerController::State::Error);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(fixture.audio.opens.size(), 1); // b never reached the player

    // The failed song stays shown for a moment, then the next one starts.
    fixture.clock.advance(249);
    QCOMPARE(fixture.audio.opens.size(), 1);
    fixture.clock.advance(1);
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("c"));
    QVERIFY(!player->stoppedAfterBrokenTracks());
}

void PlayerControllerRecoveryTest::songThePlayerCannotReadIsSkipped()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b"}));
    settle();
    fixture.audio.fail();
    QCOMPARE(player->state(), PlayerController::State::Error);

    fixture.clock.advance(250);
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    QCOMPARE(player->state(), PlayerController::State::Loading);
}

void PlayerControllerRecoveryTest::threeBrokenSongsInARowStopPlayback()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    for (const char* name : {"a", "b", "c"}) {
        fixture.resolver.broken.insert(fixture.song(QString::fromLatin1(name)).filePath);
    }
    QSignalSpy stoppedSpy(player.get(), &PlayerController::stoppedAfterBrokenTracksChanged);

    player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b", "c", "d", "e"}));
    settle();
    fixture.clock.advance(250); // a failed, b is tried
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    fixture.clock.advance(250); // b failed, c is tried
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("c"));
    QVERIFY(player->stoppedAfterBrokenTracks()); // c was the third
    QCOMPARE(stoppedSpy.count(), 1);

    fixture.clock.advance(5000);
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("c"));
    QCOMPARE(player->state(), PlayerController::State::Error);
    QVERIFY(fixture.audio.opens.isEmpty());

    // The count started over: the next press tries again and moves on.
    player->next();
    settle();
    QVERIFY(!player->stoppedAfterBrokenTracks());
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("d"));
}

void PlayerControllerRecoveryTest::aSongThatStartsResetsTheCount()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    for (const char* name : {"a", "b", "d", "e"}) {
        fixture.resolver.broken.insert(fixture.song(QString::fromLatin1(name)).filePath);
    }

    player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b", "c", "d", "e", "f"}));
    settle();
    fixture.clock.advance(500); // a and b fail
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("c"));
    fixture.audio.start(); // c plays: the count is back to 0
    fixture.audio.end();
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("d"));
    fixture.clock.advance(500); // d and e fail: two in a row only
    settle();
    QVERIFY(!player->stoppedAfterBrokenTracks());
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("f"));
}

void PlayerControllerRecoveryTest::historyIsRecordedAfterEightSecondsOfPlay()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy recordedSpy(player.get(), &PlayerController::playRecorded);
    const Track song = fixture.song(QStringLiteral("Song A"));

    player->playInContext(song, {song});
    settle();
    fixture.audio.start();
    double position = 0.0;
    playFor(fixture, 7500, position);
    QCOMPARE(recordedSpy.count(), 0);
    QCOMPARE(playCount(fixture, song), -1); // no row yet

    playFor(fixture, 500, position);
    QCOMPARE(recordedSpy.count(), 1);
    QCOMPARE(playCount(fixture, song), 1);
    QCOMPARE(lastPlayedOrder(fixture, song), 1);
}

void PlayerControllerRecoveryTest::historyCountsOnlyTimeSpentPlaying()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy recordedSpy(player.get(), &PlayerController::playRecorded);
    const Track song = fixture.song(QStringLiteral("a"));

    player->playInContext(song, {song});
    settle();
    fixture.audio.start();
    double position = 0.0;
    playFor(fixture, 5000, position);
    player->togglePlay();
    fixture.audio.paused();
    fixture.clock.advance(60000); // a long pause adds nothing
    player->togglePlay();
    fixture.audio.playing();
    playFor(fixture, 2500, position);
    QCOMPARE(recordedSpy.count(), 0);

    playFor(fixture, 500, position);
    QCOMPARE(recordedSpy.count(), 1);
}

void PlayerControllerRecoveryTest::historyIsRecordedOncePerPlay()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy recordedSpy(player.get(), &PlayerController::playRecorded);
    const QList<Track> list = fixture.songs({"a", "b"});

    player->playInContext(list.at(0), list);
    settle();
    fixture.audio.start();
    double position = 0.0;
    playFor(fixture, 30000, position);
    QCOMPARE(recordedSpy.count(), 1);
    QCOMPARE(playCount(fixture, list.at(0)), 1);

    // The next song is a new play, one further in the play order.
    fixture.audio.end();
    settle();
    fixture.audio.start();
    position = 0.0;
    playFor(fixture, 8000, position);
    QCOMPARE(recordedSpy.count(), 2);
    QCOMPARE(lastPlayedOrder(fixture, list.at(1)), 2);
    QCOMPARE(playCount(fixture, list.at(0)), 1);

    // Each round of a repeat-one song is a play of its own.
    player->setRepeatMode(lmp::core::RepeatMode::One);
    fixture.audio.end();
    settle();
    fixture.audio.playing();
    position = 0.0;
    playFor(fixture, 8000, position);
    QCOMPARE(recordedSpy.count(), 3);
    QCOMPARE(playCount(fixture, list.at(1)), 2);
    QCOMPARE(lastPlayedOrder(fixture, list.at(1)), 3);
}

void PlayerControllerRecoveryTest::supersededResolveIsIgnored()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    fixture.resolver.deferred = true;
    auto player = fixture.makeController();
    const QList<Track> list = fixture.songs({"a", "b"});

    player->playInContext(list.at(0), list);
    player->playInContext(list.at(1), list);
    QCOMPARE(fixture.resolver.calls, 2);

    fixture.resolver.finish(0); // the older answer arrives late
    QVERIFY(fixture.audio.opens.isEmpty());
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));

    fixture.resolver.finish(1);
    QCOMPARE(fixture.audio.opens.size(), 1);
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
}

void PlayerControllerRecoveryTest::localFileResolverFindsFilesOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("present.mp3"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    lmp::core::LocalFileSourceResolver resolver;
    Track present;
    present.filePath = path;
    const lmp::core::Result<QUrl> found = resolver.resolve(present).result();
    QVERIFY(found.isOk());
    QCOMPARE(found.value(), QUrl::fromLocalFile(path));

    Track missing;
    missing.filePath = dir.filePath(QStringLiteral("missing.mp3"));
    const lmp::core::Result<QUrl> gone = resolver.resolve(missing).result();
    QVERIFY(!gone.isOk());
    QCOMPARE(gone.failure().kind, lmp::core::FailureKind::NotFound);

    const lmp::core::Result<QUrl> empty = resolver.resolve(Track()).result();
    QVERIFY(!empty.isOk());
    QCOMPARE(empty.failure().kind, lmp::core::FailureKind::NotFound);
}

void PlayerControllerRecoveryTest::catalogSourceKeyMatchesStoredRows()
{
    Track local;
    local.sourceKind = QStringLiteral("local");
    local.filePath = QStringLiteral("C:\\Music\\Ärger\\Song.MP3");
    // Only A to Z are lower-cased, as SQLite's lower() does for stored keys.
    QCOMPARE(lmp::core::catalogSourceKey(local), QStringLiteral("local|c:\\music\\Ärger\\song.mp3"));

    Track remote;
    remote.sourceKind = QStringLiteral("Remote");
    remote.origin = QStringLiteral("Server");
    remote.sourceUrl = QStringLiteral("https://example.test/track/1");
    QCOMPARE(lmp::core::catalogSourceKey(remote), QStringLiteral("remote|server|https://example.test/track/1"));

    remote.origin.clear();
    QCOMPARE(lmp::core::catalogSourceKey(remote), QStringLiteral("remote|provider|https://example.test/track/1"));

    Track addressOnly;
    addressOnly.filePath = QStringLiteral("https://example.test/a.mp3");
    QCOMPARE(lmp::core::catalogSourceKey(addressOnly), QStringLiteral("remote|provider|https://example.test/a.mp3"));

    QVERIFY(lmp::core::catalogSourceKey(Track()).isEmpty());
}

QTEST_GUILESS_MAIN(PlayerControllerRecoveryTest)

#include "tst_player_controller_recovery.moc"
