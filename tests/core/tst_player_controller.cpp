#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "PlayerFixture.h"
#include "core/playback/PlaybackBackHistory.h"
#include "core/playback/QueueShuffle.h"
#include "core/settings/SettingKeys.h"

using lmp::core::PlayerController;
using lmp::core::RepeatMode;
using lmp::core::Shuffle;
using lmp::core::Track;
namespace keys = lmp::core::settings::keys;

namespace {

// Starts `title` from the context of `titles` and lets it reach Playing.
void startPlaying(PlayerFixture& fixture, PlayerController& player, const QString& title, const QStringList& titles)
{
    player.playInContext(fixture.song(title), fixture.songs(titles));
    settle();
    fixture.audio.start();
}

} // namespace

// Transport and queue rules of the player controller. Preferences are in
// tst_player_preferences; broken songs, history and resolving in
// tst_player_controller_recovery.
class PlayerControllerTest : public QObject {
    Q_OBJECT

private slots:
    void previousIntoAChangedContextKeepsTheShuffleOrder();
    void playInContextStartsTheChosenSong();
    void playInContextPlaysInListOrderAndSavesShuffleOff();
    void playNextAndAddToQueueStartPlaybackWhenIdle();
    void playNextGoesFirstAndAddToQueueGoesLast();
    void shuffleAndPlayShufflesAndSavesShuffleOn();
    void togglePlayPausesAndResumes();
    void togglePlayWithoutASongDoesNothing();
    void nextMovesOnAndWrapsWhenManual();
    void previousRestartsAfterThreeSeconds();
    void previousGoesBackWithinThreeSeconds();
    void previousUsesTheSongsPlayedBefore();
    void backHistoryKeepsTheLatestFifty();
    void seekMovesAndClampsToTheDuration();
    void clearUpcomingEmptiesTheVisibleQueue();
    void visibleQueueEditsChangeTheQueueOnly();
    void endOfSongAdvancesAndStopsAtTheEnd();
    void repeatOneReplaysOnAutomaticAdvanceOnly();
    void restoreShowsTheSongAndFirstPlayOpensItAtItsPoint();
    void restoreIsIgnoredWhileASongIsCurrent();
};

void PlayerControllerTest::playInContextStartsTheChosenSong()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy trackSpy(player.get(), &PlayerController::currentTrackChanged);

    player->playInContext(fixture.song(QStringLiteral("b")), fixture.songs({"a", "b", "c"}));
    settle();
    QCOMPARE(player->state(), PlayerController::State::Loading);
    QCOMPARE(trackSpy.count(), 1);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(fixture.audio.opens.size(), 1);
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    QVERIFY(fixture.audio.opens.last().autoplay);
    QCOMPARE(visibleTitles(*player), QStringList({"c"}));

    // The pause that means "ready" does not show while play is asked for.
    fixture.audio.ready();
    QCOMPARE(player->state(), PlayerController::State::Loading);
    QCOMPARE(player->durationSeconds(), 200.0);
    fixture.audio.playing();
    QCOMPARE(player->state(), PlayerController::State::Playing);
}

void PlayerControllerTest::playInContextPlaysInListOrderAndSavesShuffleOff()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->setShuffle(Shuffle::On);
    QVERIFY(fixture.settings.boolean(keys::ShuffleEnabled, false));

    player->playInContext(fixture.song(QStringLiteral("a")), fixture.songs({"a", "b", "c", "d"}));
    settle();
    QCOMPARE(player->shuffle(), Shuffle::Off);
    QVERIFY(!fixture.settings.boolean(keys::ShuffleEnabled, true));
    QCOMPARE(visibleTitles(*player), QStringList({"b", "c", "d"}));
}

void PlayerControllerTest::playNextAndAddToQueueStartPlaybackWhenIdle()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->playNext(fixture.songs({"x", "y"}));
    settle();
    QCOMPARE(player->currentTrack()->title, QStringLiteral("x"));
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("x"));
    QCOMPARE(visibleTitles(*player), QStringList({"y"}));

    PlayerFixture second;
    QVERIFY(second.ok());
    auto other = second.makeController();
    other->addToQueue(second.songs({"p", "q"}));
    settle();
    QCOMPARE(other->currentTrack()->title, QStringLiteral("p"));
    QCOMPARE(visibleTitles(*other), QStringList({"q"}));
}

void PlayerControllerTest::playNextGoesFirstAndAddToQueueGoesLast()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b", "c"});
    QSignalSpy queueSpy(player.get(), &PlayerController::queueChanged);

    player->addToQueue(fixture.songs({"x"}));
    player->playNext(fixture.songs({"y"}));
    QCOMPARE(queueSpy.count(), 2);
    QCOMPARE(visibleTitles(*player), QStringList({"y", "x", "b", "c"}));
    QCOMPARE(fixture.audio.opens.size(), 1); // nothing new started

    // The user queue plays before the context.
    player->next();
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("y"));
}

void PlayerControllerTest::shuffleAndPlayShufflesAndSavesShuffleOn()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    constexpr std::uint32_t seed = 11;
    auto player = fixture.makeController(seed);
    const QList<Track> list = fixture.songs({"a", "b", "c", "d", "e", "f"});

    player->shuffleAndPlay(list);
    settle();
    // The first draw of the same generator picks the song that starts.
    const QList<qsizetype> order = lmp::core::shuffledIndices(list.size(), seededRandom(seed));
    QCOMPARE(player->currentTrack()->title, list.at(order.first()).title);
    QCOMPARE(player->shuffle(), Shuffle::On);
    QVERIFY(fixture.settings.boolean(keys::ShuffleEnabled, false));
    QStringList upcoming = visibleTitles(*player);
    QCOMPARE(upcoming.size(), 5);
    upcoming.append(player->currentTrack()->title);
    upcoming.sort();
    QCOMPARE(upcoming, QStringList({"a", "b", "c", "d", "e", "f"}));
}

void PlayerControllerTest::togglePlayPausesAndResumes()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});

    player->togglePlay();
    QCOMPARE(fixture.audio.pauseCalls, 1);
    fixture.audio.paused();
    QCOMPARE(player->state(), PlayerController::State::Paused);

    player->togglePlay();
    QCOMPARE(fixture.audio.playCalls, 1);
    fixture.audio.playing();
    QCOMPARE(player->state(), PlayerController::State::Playing);
    QCOMPARE(fixture.audio.opens.size(), 1);
}

void PlayerControllerTest::togglePlayWithoutASongDoesNothing()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->togglePlay();
    settle();
    QCOMPARE(player->state(), PlayerController::State::Idle);
    QVERIFY(fixture.audio.opens.isEmpty());
    QCOMPARE(fixture.audio.playCalls, 0);
}

void PlayerControllerTest::nextMovesOnAndWrapsWhenManual()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});

    player->next();
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    fixture.audio.start();
    player->next(); // a manual step wraps round
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("a"));
}

void PlayerControllerTest::previousRestartsAfterThreeSeconds()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});
    player->next();
    settle();
    fixture.audio.start();
    fixture.audio.position(3.5);

    player->previous();
    settle();
    QCOMPARE(fixture.audio.seeks, QList<double>({0.0}));
    QCOMPARE(player->positionSeconds(), 0.0);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(fixture.audio.opens.size(), 2);
}

void PlayerControllerTest::previousGoesBackWithinThreeSeconds()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b", "c"});
    player->next();
    settle();
    fixture.audio.start();
    fixture.audio.position(3.0); // not more than 3 s

    player->previous();
    settle();
    QVERIFY(fixture.audio.seeks.isEmpty());
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("a"));
    QCOMPARE(visibleTitles(*player), QStringList({"b", "c"}));
}

void PlayerControllerTest::previousUsesTheSongsPlayedBefore()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b", "c", "d", "e"});

    player->playVisibleQueueEntry(3); // e, jumping over b, c, d
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("e"));
    fixture.audio.start();

    // Back to where the jump came from, not to the song before e in the list.
    player->previous();
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("a"));
    QCOMPARE(visibleTitles(*player), QStringList({"b", "c", "d", "e"}));

    // With the history used up, Previous steps through the list (wrapping).
    fixture.audio.start();
    player->previous();
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("e"));
}

void PlayerControllerTest::backHistoryKeepsTheLatestFifty()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    QStringList names;
    for (int index = 0; index < 60; ++index) {
        names.append(QStringLiteral("song%1").arg(index));
    }
    const QList<Track> context = fixture.songs(names);
    lmp::core::PlaybackBackHistory history;
    lmp::core::QueueEngine queue = lmp::core::QueueEngine::create(context, 0);
    for (int index = 0; index < 55; ++index) {
        history.remember(context.at(index), queue);
    }
    QCOMPARE(history.size(), lmp::core::PlaybackBackHistory::Capacity);

    // The newest first; the five oldest were forgotten.
    for (int index = 54; index >= 5; --index) {
        const auto restored = history.restorePrevious(queue);
        QVERIFY(restored.has_value());
        QCOMPARE(restored->track.title, names.at(index));
    }
    QVERIFY(!history.restorePrevious(queue).has_value());
}

void PlayerControllerTest::seekMovesAndClampsToTheDuration()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a"});

    player->seek(42.5);
    QCOMPARE(fixture.audio.seeks.last(), 42.5);
    QCOMPARE(player->positionSeconds(), 42.5);
    player->seek(999.0);
    QCOMPARE(fixture.audio.seeks.last(), 200.0);
    player->seek(-3.0);
    QCOMPARE(fixture.audio.seeks.last(), 0.0);
}

void PlayerControllerTest::clearUpcomingEmptiesTheVisibleQueue()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b", "c"});
    player->addToQueue(fixture.songs({"x"}));

    player->clearUpcoming();
    QVERIFY(player->visibleQueue().isEmpty());
    QCOMPARE(player->currentTrack()->title, QStringLiteral("a"));
    QCOMPARE(player->state(), PlayerController::State::Playing);
}

void PlayerControllerTest::visibleQueueEditsChangeTheQueueOnly()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b", "c", "d"});
    player->addToQueue(fixture.songs({"x"}));
    QCOMPARE(visibleTitles(*player), QStringList({"x", "b", "c", "d"}));
    QSignalSpy queueSpy(player.get(), &PlayerController::queueChanged);
    const auto opens = fixture.audio.opens.size();

    const lmp::core::QueueEntry removed = player->visibleQueue().at(2);
    player->removeVisibleEntry(2);
    QCOMPARE(visibleTitles(*player), QStringList({"x", "b", "d"}));
    player->restoreVisibleEntry(2, removed);
    QCOMPARE(visibleTitles(*player), QStringList({"x", "b", "c", "d"}));
    player->moveVisibleEntry(3, lmp::core::QueueDirection::Previous);
    QCOMPARE(visibleTitles(*player), QStringList({"x", "b", "d", "c"}));
    player->commitVisibleOrder(fixture.songs({"c", "x", "b", "d"}));
    QCOMPARE(visibleTitles(*player), QStringList({"c", "x", "b", "d"}));
    QCOMPARE(queueSpy.count(), 4);

    // Rows outside the queue and edits that change nothing emit nothing.
    player->removeVisibleEntry(9);
    player->moveVisibleEntry(0, lmp::core::QueueDirection::Previous);
    player->restoreVisibleEntry(1, removed);
    QCOMPARE(queueSpy.count(), 4);

    // The current song plays on untouched.
    QCOMPARE(player->currentTrack()->title, QStringLiteral("a"));
    QCOMPARE(player->state(), PlayerController::State::Playing);
    QCOMPARE(fixture.audio.opens.size(), opens);
}

void PlayerControllerTest::endOfSongAdvancesAndStopsAtTheEnd()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy endedSpy(player.get(), &PlayerController::trackEnded);
    QSignalSpy extensionSpy(player.get(), &PlayerController::autoplayExtensionWanted);
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});

    fixture.audio.end();
    settle();
    QCOMPARE(endedSpy.count(), 1);
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    fixture.audio.start();

    // Repeat off: the automatic step after the last song ends the queue.
    extensionSpy.clear();
    fixture.audio.end();
    settle();
    QCOMPARE(fixture.audio.opens.size(), 2);
    QCOMPARE(player->state(), PlayerController::State::Paused);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(player->positionSeconds(), 200.0);
    QCOMPARE(extensionSpy.count(), 1);
    QCOMPARE(extensionSpy.first().first().toBool(), true);
}

void PlayerControllerTest::repeatOneReplaysOnAutomaticAdvanceOnly()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    player->setRepeatMode(RepeatMode::One);
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});

    fixture.audio.end();
    settle();
    QCOMPARE(fixture.audio.opens.size(), 1); // the same source again
    QCOMPARE(fixture.audio.playCalls, 1);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("a"));
    QCOMPARE(player->positionSeconds(), 0.0);
    fixture.audio.playing();

    player->next(); // a manual step moves on
    settle();
    QCOMPARE(fixture.audio.opens.size(), 2);
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
}

void PlayerControllerTest::restoreShowsTheSongAndFirstPlayOpensItAtItsPoint()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    QSignalSpy leftSpy(player.get(), &PlayerController::trackLeft);
    const QList<Track> songs = fixture.songs({"a", "b", "c"});
    player->restore(lmp::core::QueueEngine::create(songs, 1), songs.at(1), 75.0);

    QCOMPARE(player->currentTrack()->title, QStringLiteral("b"));
    QCOMPARE(player->state(), PlayerController::State::Idle);
    QCOMPARE(player->positionSeconds(), 75.0);
    QCOMPARE(player->durationSeconds(), 200.0);
    QCOMPARE(visibleTitles(*player), QStringList{QStringLiteral("c")});
    QVERIFY(fixture.audio.opens.isEmpty());
    QCOMPARE(fixture.resolver.calls, 0);
    QVERIFY(leftSpy.isEmpty()); // nothing was left

    player->togglePlay();
    settle();
    QCOMPARE(fixture.audio.opens.size(), 1);
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("b"));
    QCOMPARE(fixture.audio.opens.first().startAtSeconds, 75.0);
    QVERIFY(fixture.audio.opens.first().autoplay);
    fixture.audio.start();
    QCOMPARE(player->state(), PlayerController::State::Playing);

    player->next();
    settle();
    QCOMPARE(lastOpenedTitle(fixture.audio), QStringLiteral("c"));
}

void PlayerControllerTest::restoreIsIgnoredWhileASongIsCurrent()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    auto player = fixture.makeController();
    startPlaying(fixture, *player, QStringLiteral("a"), {"a", "b"});
    const QList<Track> other = fixture.songs({"x", "y"});
    player->restore(lmp::core::QueueEngine::create(other, 0), other.at(0), 40.0);
    QCOMPARE(player->currentTrack()->title, QStringLiteral("a"));
    QCOMPARE(visibleTitles(*player), QStringList{QStringLiteral("b")});
    QCOMPARE(player->state(), PlayerController::State::Playing);
}

void PlayerControllerTest::previousIntoAChangedContextKeepsTheShuffleOrder()
{
    PlayerFixture fixture;
    QVERIFY(fixture.ok());
    // A song of playlist A was playing; then album B started, shuffled.
    const QList<Track> playlist = fixture.songs({"x"});
    lmp::core::PlaybackBackHistory history;
    history.remember(playlist.first(), lmp::core::QueueEngine::create(playlist, 0));
    lmp::core::QueueState album;
    album.contextQueue = fixture.songs({"p", "q", "r", "s"});
    album.contextIndex = 2;
    album.shuffle = true;
    album.shuffleOrder = {2, 0, 3, 1}; // r, p, s, q
    const auto restored = history.restorePrevious(lmp::core::QueueEngine::fromState(album));
    QVERIFY(restored.has_value());
    QCOMPARE(restored->track.title, QStringLiteral("x"));
    QVERIFY(restored->queue.state().shuffle);
    // The album's songs come after it in the order they were going to play.
    QStringList upcoming;
    for (const lmp::core::QueueEntry& entry : restored->queue.visibleQueue()) {
        upcoming.append(entry.track.title);
    }
    QCOMPARE(upcoming, QStringList({QStringLiteral("r"), QStringLiteral("p"), QStringLiteral("s"), QStringLiteral("q")}));
}

QTEST_GUILESS_MAIN(PlayerControllerTest)

#include "tst_player_controller.moc"
