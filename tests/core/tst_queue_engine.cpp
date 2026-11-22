#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTest>

#include "core/model/Track.h"
#include "core/playback/QueueEngine.h"
#include "core/playback/QueueShuffle.h"
#include "core/playback/QueueSnapshot.h"
#include "core/playback/QueueTrackIdentity.h"
#include "core/playback/QueueTypes.h"

#include <optional>

using lmp::core::AdvanceTrigger;
using lmp::core::QueueAdvance;
using lmp::core::QueueDirection;
using lmp::core::QueueEngine;
using lmp::core::QueueEntry;
using lmp::core::QueueEntryKind;
using lmp::core::QueueRandom;
using lmp::core::QueueSnapshot;
using lmp::core::QueueSnapshotLimits;
using lmp::core::QueueState;
using lmp::core::RepeatMode;
using lmp::core::Shuffle;
using lmp::core::Track;

namespace {

constexpr AdvanceTrigger kManual = AdvanceTrigger::Manual;
constexpr AdvanceTrigger kAutomatic = AdvanceTrigger::Automatic;
constexpr QueueDirection kNext = QueueDirection::Next;
constexpr QueueDirection kEarlier = QueueDirection::Previous;

// A remote song whose server id is `id`, shaped like the test songs of the
// original suite.
Track track(const QString& id)
{
    Track song;
    song.remoteId = id;
    song.sourceKind = QStringLiteral("remote");
    song.sourceTag = QStringLiteral("tag-a");
    song.title = id;
    song.artist = QStringLiteral("Artist");
    song.durationSeconds = 60;
    song.sourceUrl = id;
    song.streamUrl = id;
    return song;
}

QList<Track> tracks(const QStringList& ids)
{
    QList<Track> list;
    for (const QString& id : ids) {
        list.append(track(id));
    }
    return list;
}

// t0 .. t(count-1).
QList<Track> numberedTracks(int count)
{
    QList<Track> list;
    for (int index = 0; index < count; ++index) {
        list.append(track(QStringLiteral("t%1").arg(index)));
    }
    return list;
}

QStringList ids(const QList<QueueEntry>& entries)
{
    QStringList list;
    for (const QueueEntry& entry : entries) {
        list.append(entry.track.remoteId);
    }
    return list;
}

QStringList ids(const QList<Track>& list)
{
    QStringList result;
    for (const Track& song : list) {
        result.append(song.remoteId);
    }
    return result;
}

QString idOf(const std::optional<Track>& song)
{
    return song ? song->remoteId : QString();
}

QueueRandom constant(double value)
{
    return [value]() { return value; };
}

Track localFile(const QString& path)
{
    Track song;
    song.sourceKind = QStringLiteral("local");
    song.filePath = path;
    song.title = path;
    return song;
}

} // namespace

class QueueEngineTest : public QObject {
    Q_OBJECT

private slots:
    // One slot per case of the original suite, in its order.
    void explicitQueuePlaysBeforePlaybackContextAndSurvivesContextChanges();
    void naturalEndStopsWhenRepeatIsOffWhileManualNextWraps();
    void queueEntriesCanMoveRemoveAndAcceptDeduplicatedAutoplayTracks();
    void shuffleExposesItsStableOrderAndKeepsThatOrderAfterAnEdit();
    void shuffleKeepsEveryUnplayedContextTrackReachable();
    void removedQueueEntriesRestoreAtASafePositionWithoutDuplicatingLaterChanges();
    void aSnapshotKeepsEveryUpcomingSongOfALongQueueAndTrimsOnlyPlayedOnes();
    void aShuffledQueueRestoresInTheOrderItWasGoingToPlay();
    void aFallbackSnapshotIsABoundedWindowAroundTheCurrentSong();
    void aSnapshotWithNoCurrentSongKeepsTheQueueFromItsStart();

    // Additional cases for behaviour the original suite does not reach.
    void localFilesUseTheirPathAsIdentity();
    void repeatAllWrapsTheVisibleQueue();
    void upcomingCountFollowsThePlayOrder();
    void emptyQueueEndsAndIgnoresEdits();
    void fromStateDropsAShuffleOrderThatIsNotAPermutation();
    void generatorOutsideItsRangeStillShufflesEveryIndex();
    void snapshotJsonRoundTrip();
    void malformedSnapshotJsonFallsBack();
};

void QueueEngineTest::explicitQueuePlaysBeforePlaybackContextAndSurvivesContextChanges()
{
    QueueEngine core = QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}), 0);
    core = core.appendUserQueue(tracks({QStringLiteral("x"), QStringLiteral("y")}));
    core = core.insertPlayNext(tracks({QStringLiteral("z")}));
    QCOMPARE(ids(core.visibleQueue()),
             QStringList({QStringLiteral("z"), QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("b"),
                          QStringLiteral("c")}));
    const QueueAdvance first = core.advance(kNext, kManual);
    QCOMPARE(idOf(first.track), QStringLiteral("z"));
    const QueueAdvance second = first.queue.advance(kNext, kAutomatic);
    QCOMPARE(idOf(second.track), QStringLiteral("x"));
    const QueueEngine context = QueueEngine::create(tracks({QStringLiteral("new"), QStringLiteral("later")}), 0,
                                                    second.queue.state().userQueue);
    QCOMPARE(ids(context.visibleQueue()), QStringList({QStringLiteral("y"), QStringLiteral("later")}));
}

void QueueEngineTest::naturalEndStopsWhenRepeatIsOffWhileManualNextWraps()
{
    const QueueEngine core = QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b")}), 1);
    QCOMPARE(core.advance(kNext, kAutomatic).ended, true);
    QCOMPARE(idOf(core.advance(kNext, kManual).track), QStringLiteral("a"));
    const QueueEngine repeatAll = core.withRepeatMode(RepeatMode::All);
    QCOMPARE(idOf(repeatAll.advance(kNext, kAutomatic).track), QStringLiteral("a"));
    const QueueEngine repeatOne = core.withRepeatMode(RepeatMode::One);
    QCOMPARE(idOf(repeatOne.advance(kNext, kAutomatic).track), QStringLiteral("b"));
    QCOMPARE(idOf(repeatOne.advance(kNext, kManual).track), QStringLiteral("a"));
}

void QueueEngineTest::queueEntriesCanMoveRemoveAndAcceptDeduplicatedAutoplayTracks()
{
    QueueEngine core = QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}), 0,
                                           tracks({QStringLiteral("x")}));
    core = core.moveVisibleEntry(2, kEarlier);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("x"), QStringLiteral("c"), QStringLiteral("b")}));
    core = core.removeVisibleEntry(0);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("c"), QStringLiteral("b")}));
    core = core.appendAutoplayTracks(tracks({QStringLiteral("c"), QStringLiteral("radio1"), QStringLiteral("radio2")}));
    QCOMPARE(ids(core.state().contextQueue),
             QStringList({QStringLiteral("a"), QStringLiteral("c"), QStringLiteral("b"), QStringLiteral("radio1"),
                          QStringLiteral("radio2")}));
}

void QueueEngineTest::shuffleExposesItsStableOrderAndKeepsThatOrderAfterAnEdit()
{
    QueueEngine core = QueueEngine::create(
        tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}), 0, {},
        Shuffle::On, RepeatMode::Off, constant(0.999));
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}));
    core = core.moveVisibleEntry(2, kEarlier);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("b"), QStringLiteral("d"), QStringLiteral("c")}));
    core = core.removeVisibleEntry(0);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("d"), QStringLiteral("c")}));
}

void QueueEngineTest::shuffleKeepsEveryUnplayedContextTrackReachable()
{
    const QueueEngine core = QueueEngine::create(
        tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")}), 2, {},
        Shuffle::Off, RepeatMode::Off, constant(0));
    const QueueEngine shuffled = core.withShuffle(Shuffle::On, constant(0));
    // The current track must anchor the shuffle traversal.
    QCOMPARE(shuffled.state().shuffleOrder.value(0, -1), 2);
    QStringList seen;
    QueueEngine next = shuffled;
    while (true) {
        const QueueAdvance result = next.advance(kNext, kAutomatic, constant(0));
        if (!result.track) {
            break;
        }
        seen.append(result.track->remoteId);
        next = result.queue;
    }
    QCOMPARE(seen, QStringList({QStringLiteral("b"), QStringLiteral("d"), QStringLiteral("a")}));

    const QueueEngine unshuffled = next.withShuffle(Shuffle::Off, constant(0));
    QCOMPARE(unshuffled.state().shuffle, false);
    QCOMPARE(unshuffled.state().shuffleOrder.size(), 0);
}

void QueueEngineTest::removedQueueEntriesRestoreAtASafePositionWithoutDuplicatingLaterChanges()
{
    QueueEngine core = QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}), 0,
                                           tracks({QStringLiteral("x")}));
    const QueueEntry user = core.visibleQueue().at(0);
    core = core.removeVisibleEntry(0);
    core = core.restoreVisibleEntry(99, user);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("x"), QStringLiteral("b"), QStringLiteral("c")}));

    const QueueEntry context = core.visibleQueue().at(1);
    core = core.removeVisibleEntry(1);
    core = core.restoreVisibleEntry(1, context);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("x"), QStringLiteral("b"), QStringLiteral("c")}));
    QCOMPARE(core.restoreVisibleEntry(0, context), core);

    const QueueEngine shuffled = QueueEngine::create(
        tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}), 0, {}, Shuffle::On, RepeatMode::Off,
        constant(0.999));
    const QueueEntry removed = shuffled.visibleQueue().at(0);
    const QueueEngine restored = shuffled.removeVisibleEntry(0).restoreVisibleEntry(-5, removed);
    const QStringList restoredIds = ids(restored.visibleQueue());
    QCOMPARE(QSet<QString>(restoredIds.cbegin(), restoredIds.cend()),
             QSet<QString>({QStringLiteral("b"), QStringLiteral("c")}));
}

void QueueEngineTest::aSnapshotKeepsEveryUpcomingSongOfALongQueueAndTrimsOnlyPlayedOnes()
{
    const QueueEngine core = QueueEngine::create(numberedTracks(500), 120);
    QueueSnapshotLimits limits;
    limits.played = 50;
    const QueueSnapshot snapshot = core.snapshot(limits);
    QCOMPARE(snapshot.contextQueue.size(), 50 + 1 + 379);
    QCOMPARE(snapshot.contextQueue.at(snapshot.contextIndex).remoteId, QStringLiteral("t120"));
    QCOMPARE(snapshot.contextQueue.first().remoteId, QStringLiteral("t70"));
    QCOMPARE(snapshot.contextQueue.last().remoteId, QStringLiteral("t499"));
}

void QueueEngineTest::aShuffledQueueRestoresInTheOrderItWasGoingToPlay()
{
    // A Park-Miller generator, so the shuffle is the same on every run.
    qint64 seed = 7;
    const QueueRandom random = [&seed]() {
        seed = (seed * 16807) % 2147483647;
        return static_cast<double>(seed) / 2147483647.0;
    };
    const QueueEngine core = QueueEngine::create(numberedTracks(30), 0, {}, Shuffle::On, RepeatMode::Off, random);
    // As in the original case, only the shuffle draws from the seeded
    // generator; the steps use the default one and never need it.
    const QueueEngine played = core.advance(kNext, kAutomatic).queue.advance(kNext, kAutomatic).queue;
    const QStringList upcoming = ids(played.visibleQueue());

    const QueueEngine restored =
        QueueEngine::restored(played.snapshot(QueueSnapshotLimits{}), Shuffle::On, RepeatMode::Off);
    QCOMPARE(restored.state().shuffle, true);
    QCOMPARE(idOf(restored.currentContextTrack()), idOf(played.currentContextTrack()));
    QCOMPARE(ids(restored.visibleQueue()), upcoming);
    QueueAdvance next = restored.advance(kNext, kAutomatic);
    QCOMPARE(idOf(next.track), upcoming.value(0));
    next = next.queue.advance(kNext, kAutomatic);
    QCOMPARE(idOf(next.track), upcoming.value(1));
}

void QueueEngineTest::aFallbackSnapshotIsABoundedWindowAroundTheCurrentSong()
{
    QList<Track> userTracks;
    for (const Track& song : numberedTracks(80)) {
        userTracks.append(track(QStringLiteral("u-") + song.remoteId));
    }
    const QueueEngine core = QueueEngine::create(numberedTracks(500), 300).appendUserQueue(userTracks);
    QueueSnapshotLimits limits;
    limits.played = 50;
    limits.upcoming = 150;
    limits.user = 50;
    const QueueSnapshot snapshot = core.snapshot(limits);
    QCOMPARE(snapshot.contextQueue.size(), 201);
    QCOMPARE(snapshot.contextQueue.at(snapshot.contextIndex).remoteId, QStringLiteral("t300"));
    QCOMPARE(snapshot.contextQueue.first().remoteId, QStringLiteral("t250"));
    QCOMPARE(snapshot.contextQueue.last().remoteId, QStringLiteral("t450"));
    QCOMPARE(snapshot.userQueue.size(), 50);
}

void QueueEngineTest::aSnapshotWithNoCurrentSongKeepsTheQueueFromItsStart()
{
    QueueState state = QueueEngine::create(numberedTracks(5), 0).state();
    state.contextIndex = -1;
    const QueueSnapshot snapshot = QueueEngine::fromState(state).snapshot(QueueSnapshotLimits{});
    QCOMPARE(snapshot.contextIndex, -1);
    QCOMPARE(ids(snapshot.contextQueue),
             QStringList({QStringLiteral("t0"), QStringLiteral("t1"), QStringLiteral("t2"), QStringLiteral("t3"),
                          QStringLiteral("t4")}));
}

void QueueEngineTest::localFilesUseTheirPathAsIdentity()
{
    const Track first = localFile(QStringLiteral("D:\\Music\\one.flac"));
    const Track second = localFile(QStringLiteral("D:\\Music\\two.flac"));
    const Track noPath = localFile(QString());
    QCOMPARE(lmp::core::queueTrackIdentity(first), QStringLiteral("local|D:\\Music\\one.flac"));
    QCOMPARE(lmp::core::queueTrackIdentity(noPath), QString());
    // A local path and a server id with the same text are different songs.
    const Track remoteTwin = track(first.filePath);
    QVERIFY(lmp::core::queueTrackIdentity(remoteTwin) != lmp::core::queueTrackIdentity(first));

    const QueueEngine core = QueueEngine::create({first, second, first, noPath}, 0, {first, remoteTwin});
    QCOMPARE(core.state().contextQueue, QList<Track>({first, second}));
    QCOMPARE(core.state().userQueue, QList<Track>({first, remoteTwin}));
    QCOMPARE(core.appendAutoplayTracks({second}), core);
}

void QueueEngineTest::repeatAllWrapsTheVisibleQueue()
{
    const QueueEngine core =
        QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}), 1)
            .withRepeatMode(RepeatMode::All);
    QCOMPARE(ids(core.visibleQueue()), QStringList({QStringLiteral("c"), QStringLiteral("a")}));
    QCOMPARE(core.upcomingCount(), 1);
    // A manual previous from the first song wraps to the last.
    QCOMPARE(idOf(QueueEngine::create(tracks({QStringLiteral("a"), QStringLiteral("b")}), 0)
                      .advance(QueueDirection::Previous, kManual)
                      .track),
             QStringLiteral("b"));
}

void QueueEngineTest::upcomingCountFollowsThePlayOrder()
{
    const QueueEngine plain = QueueEngine::create(numberedTracks(4), 1, tracks({QStringLiteral("x")}));
    QCOMPARE(plain.upcomingCount(), 1 + 2);
    // With 0.999 the order stays 0 1 2 3 after the anchor, so from index 1
    // the play order is 1 0 2 3: three songs to come.
    const QueueEngine shuffled =
        QueueEngine::create(numberedTracks(4), 1, {}, Shuffle::On, RepeatMode::Off, constant(0.999));
    QCOMPARE(shuffled.state().shuffleOrder, QList<qsizetype>({1, 0, 2, 3}));
    QCOMPARE(shuffled.upcomingCount(), 3);
}

void QueueEngineTest::emptyQueueEndsAndIgnoresEdits()
{
    const QueueEngine empty{};
    QCOMPARE(empty.state().contextIndex, -1);
    const QueueAdvance result = empty.advance(kNext, kManual);
    QVERIFY(result.ended);
    QVERIFY(!result.track);
    QVERIFY(empty.visibleQueue().isEmpty());
    QCOMPARE(empty.removeVisibleEntry(0), empty);
    QCOMPARE(empty.moveVisibleEntry(0, kNext), empty);
    QCOMPARE(empty.removeVisibleEntry(-1), empty);
    QVERIFY(!empty.currentContextTrack());
}

void QueueEngineTest::fromStateDropsAShuffleOrderThatIsNotAPermutation()
{
    QueueState state = QueueEngine::create(numberedTracks(3), 0).state();
    state.shuffle = true;
    state.shuffleOrder = {0, 7, 1};
    const QueueEngine queue = QueueEngine::fromState(state);
    QVERIFY(queue.state().shuffleOrder.isEmpty());
    QCOMPARE(ids(queue.visibleQueue()), QStringList({QStringLiteral("t1"), QStringLiteral("t2")}));
}

void QueueEngineTest::generatorOutsideItsRangeStillShufflesEveryIndex()
{
    for (const double value : {1.0, 5.0, -3.0}) {
        const QList<qsizetype> order = lmp::core::anchoredShuffleOrder(6, 2, constant(value));
        QVERIFY(lmp::core::isPermutation(order, 6));
        QCOMPARE(order.value(0, -1), 2);
    }
}

void QueueEngineTest::snapshotJsonRoundTrip()
{
    QueueSnapshot snapshot;
    snapshot.contextQueue = {localFile(QStringLiteral("D:\\Music\\one.flac")),
                             localFile(QStringLiteral("D:\\Music\\two.flac"))};
    snapshot.contextIndex = 1;
    snapshot.userQueue = {localFile(QStringLiteral("D:\\Music\\three.flac"))};
    const QJsonObject json = snapshot.toJson();
    QStringList keys = json.keys();
    keys.sort();
    QCOMPARE(keys,
             QStringList({QStringLiteral("contextIndex"), QStringLiteral("contextQueue"), QStringLiteral("userQueue")}));
    QCOMPARE(json.value(QStringLiteral("contextIndex")).toInt(), 1);
    QCOMPARE(json.value(QStringLiteral("contextQueue")).toArray().size(), 2);

    const QueueSnapshot back = QueueSnapshot::fromJson(json);
    QCOMPARE(back.contextIndex, 1);
    QCOMPARE(back.contextQueue.size(), 2);
    QCOMPARE(back.contextQueue.at(1).filePath, QStringLiteral("D:\\Music\\two.flac"));
    QCOMPARE(back.userQueue.at(0).filePath, QStringLiteral("D:\\Music\\three.flac"));
    const std::optional<Track> current = QueueEngine::restored(back, Shuffle::Off, RepeatMode::Off).currentContextTrack();
    QVERIFY(current.has_value());
    QCOMPARE(current->filePath, QStringLiteral("D:\\Music\\two.flac"));
}

void QueueEngineTest::malformedSnapshotJsonFallsBack()
{
    QJsonObject json;
    json.insert(QStringLiteral("contextQueue"), QStringLiteral("not a list"));
    json.insert(QStringLiteral("contextIndex"), 1.5);
    json.insert(QStringLiteral("userQueue"),
                QJsonArray({42, localFile(QStringLiteral("D:\\Music\\one.flac")).toSnapshotJson()}));
    const QueueSnapshot snapshot = QueueSnapshot::fromJson(json);
    QVERIFY(snapshot.contextQueue.isEmpty());
    QCOMPARE(snapshot.contextIndex, -1);
    QCOMPARE(snapshot.userQueue.size(), 1);

    QCOMPARE(QueueSnapshot::fromJson(QJsonObject()), QueueSnapshot());
    json.insert(QStringLiteral("contextIndex"), QStringLiteral("2"));
    QCOMPARE(QueueSnapshot::fromJson(json).contextIndex, -1);
    json.insert(QStringLiteral("contextIndex"), 1e12);
    QCOMPARE(QueueSnapshot::fromJson(json).contextIndex, 2147483647);

    // A saved index past the end lands on the last song when restored.
    QueueSnapshot saved;
    saved.contextQueue = numberedTracks(3);
    saved.contextIndex = 2147483647;
    QCOMPARE(QueueEngine::restored(saved, Shuffle::Off, RepeatMode::Off).state().contextIndex, 2);
}

QTEST_GUILESS_MAIN(QueueEngineTest)
#include "tst_queue_engine.moc"
