#include "core/playback/AutoplayService.h"
#include "core/playback/QueueTrackIdentity.h"

#include <QPromise>
#include <memory>
#include <vector>
#include <QtTest/QtTest>

namespace core = lmp::core;

namespace {

core::Track song(const QString& id)
{
    core::Track track;
    track.title = QStringLiteral("Song ") + id;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("remote");
    track.sourceUrl = QStringLiteral("https://music.example.test/") + id;
    track.remoteId = QStringLiteral("tag-b:") + id;
    return track;
}

QList<core::Track> songs(const QStringList& ids)
{
    QList<core::Track> list;
    for (const QString& id : ids) {
        list.append(song(id));
    }
    return list;
}

class FakeQueue final : public core::AutoplayQueue {
public:
    std::optional<core::Track> current;
    bool autoplayOn = true;
    bool repeatIsOff = true;
    QList<core::Track> queued;
    qsizetype upcoming = 0;
    QList<core::Track> appended;
    bool resumed = false;
    int appends = 0;

    [[nodiscard]] std::optional<core::Track> currentTrack() const override { return current; }
    [[nodiscard]] bool autoplay() const override { return autoplayOn; }
    [[nodiscard]] bool repeatOff() const override { return repeatIsOff; }
    [[nodiscard]] qsizetype upcomingCount() const override { return upcoming; }
    [[nodiscard]] QList<core::Track> queuedTracks() const override { return queued; }
    void appendAutoplayTracks(const QList<core::Track>& tracks, bool resume) override
    {
        ++appends;
        appended += tracks;
        queued += tracks;
        upcoming += tracks.size();
        resumed = resumed || resume;
    }
    void playAlone(const core::Track& track) override
    {
        current = track;
        queued = {track};
        upcoming = 0;
    }
};

// A source whose answers the test releases by hand.
struct FakeSource {
    int relatedCalls = 0;
    int searchCalls = 0;
    QList<core::Track> history;
    std::optional<QPromise<core::Result<QList<core::Track>>>> relatedPending;
    std::optional<QPromise<core::Result<QList<core::Track>>>> searchPending;

    core::AutoplaySource source()
    {
        return {.related =
                    [this](const core::Track&) {
                        ++relatedCalls;
                        relatedPending.emplace();
                        relatedPending->start();
                        return relatedPending->future();
                    },
                .search =
                    [this](const QString&) {
                        ++searchCalls;
                        searchPending.emplace();
                        searchPending->start();
                        return searchPending->future();
                    },
                .history = [this]() { return history; }};
    }

    // Answers the pending request. Finished promises are kept until the
    // test ends, so none is destroyed before its continuation has run.
    static void answer(std::optional<QPromise<core::Result<QList<core::Track>>>>& pending,
                       const core::Result<QList<core::Track>>& result)
    {
        QVERIFY(pending.has_value());
        pending->addResult(result);
        pending->finish();
        finished().push_back(std::make_shared<QPromise<core::Result<QList<core::Track>>>>(std::move(*pending)));
        pending.reset();
    }

    static std::vector<std::shared_ptr<QPromise<core::Result<QList<core::Track>>>>>& finished()
    {
        static std::vector<std::shared_ptr<QPromise<core::Result<QList<core::Track>>>>> kept;
        return kept;
    }
};

QStringList titles(const QList<core::Track>& tracks)
{
    QStringList list;
    for (const core::Track& track : tracks) {
        list.append(track.title);
    }
    return list;
}

} // namespace

class AutoplayServiceTest : public QObject {
    Q_OBJECT

private slots:
    void topsUpNearTheEnd();
    void leavesAFullQueueAlone();
    void dropsDuplicatesAndCapsAt20();
    void searchesWhenRelatedFindsNothing();
    void resumesAQueueThatEnded();
    void staleAnswersAreDropped();
    void oneRequestAtATime();
    void mixWithAutoplayOffAddsOnce();
};

void AutoplayServiceTest::topsUpNearTheEnd()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    queue.queued = songs({QStringLiteral("seed"), QStringLiteral("a"), QStringLiteral("b")});
    queue.upcoming = 2;
    autoplay.extensionWanted(false);
    QCOMPARE(fake.relatedCalls, 1);
    QVERIFY(autoplay.loading());
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("c"), QStringLiteral("d")}));
    QTRY_COMPARE(queue.appends, 1);
    QCOMPARE(titles(queue.appended), (QStringList{QStringLiteral("Song c"), QStringLiteral("Song d")}));
    QVERIFY(!queue.resumed);
    QVERIFY(!autoplay.loading());
}

void AutoplayServiceTest::leavesAFullQueueAlone()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    queue.upcoming = 3;
    autoplay.extensionWanted(false);
    queue.upcoming = 0;
    queue.autoplayOn = false;
    autoplay.extensionWanted(true);
    queue.autoplayOn = true;
    queue.repeatIsOff = false;
    autoplay.extensionWanted(true);
    QCOMPARE(fake.relatedCalls, 0);
    QCOMPARE(fake.searchCalls, 0);
}

void AutoplayServiceTest::dropsDuplicatesAndCapsAt20()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    queue.queued = songs({QStringLiteral("seed"), QStringLiteral("q")});
    fake.history = songs({QStringLiteral("h")});
    autoplay.extensionWanted(false);
    QList<core::Track> candidates = songs({QStringLiteral("seed"), QStringLiteral("q"), QStringLiteral("h"),
                                           QStringLiteral("x"), QStringLiteral("x")});
    core::Track broken = song(QStringLiteral("broken"));
    broken.unavailable = true;
    core::Track nowhere = song(QStringLiteral("nowhere"));
    nowhere.sourceUrl.clear();
    nowhere.title = QStringLiteral("Unplayable");
    candidates << broken << nowhere;
    for (int index = 0; index < 30; ++index) {
        candidates.append(song(QStringLiteral("n%1").arg(index)));
    }
    FakeSource::answer(fake.relatedPending, candidates);
    QTRY_COMPARE(queue.appends, 1);
    QCOMPARE(queue.appended.size(), 20);
    QCOMPARE(queue.appended.first().title, QStringLiteral("Song x"));
    QCOMPARE(queue.appended.at(1).title, QStringLiteral("Song n0"));
    // Songs this run added are not added again, even once they leave the queue.
    queue.queued = {*queue.current};
    queue.upcoming = 0;
    autoplay.extensionWanted(false);
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("x"), QStringLiteral("n0"), QStringLiteral("new")}));
    QTRY_COMPARE(queue.appends, 2);
    QCOMPARE(queue.appended.constLast().title, QStringLiteral("Song new"));
    // A new queue forgets them.
    autoplay.contextReplaced();
    queue.queued = {*queue.current};
    autoplay.extensionWanted(false);
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("x")}));
    QTRY_COMPARE(queue.appends, 3);
}

void AutoplayServiceTest::searchesWhenRelatedFindsNothing()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    autoplay.extensionWanted(false);
    FakeSource::answer(fake.relatedPending,
                       core::Result<QList<core::Track>>(core::Failure::of(core::FailureKind::NotFound, QStringLiteral("old service"))));
    QTRY_COMPARE(fake.searchCalls, 1);
    FakeSource::answer(fake.searchPending, songs({QStringLiteral("s")}));
    QTRY_COMPARE(queue.appends, 1);
    // A song without a link goes straight to the search.
    core::Track bare = song(QStringLiteral("bare"));
    bare.sourceUrl.clear();
    queue.current = bare;
    autoplay.contextReplaced();
    autoplay.extensionWanted(false);
    QCOMPARE(fake.relatedCalls, 1);
    QCOMPARE(fake.searchCalls, 2);
    FakeSource::answer(fake.searchPending, songs({}));
    QTest::qWait(20);
    QCOMPARE(queue.appends, 1);
    QVERIFY(!autoplay.loading());
}

void AutoplayServiceTest::resumesAQueueThatEnded()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    autoplay.extensionWanted(false);
    // The queue runs out while the request is on its way.
    autoplay.extensionWanted(true);
    QCOMPARE(fake.relatedCalls, 1);
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("c")}));
    QTRY_COMPARE(queue.appends, 1);
    QVERIFY(queue.resumed);
}

void AutoplayServiceTest::staleAnswersAreDropped()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    autoplay.extensionWanted(true);
    autoplay.invalidate();
    QVERIFY(!autoplay.loading());
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("c")}));
    // Autoplay turned off while asking: nothing is added either.
    autoplay.extensionWanted(false);
    queue.autoplayOn = false;
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("d")}));
    QTest::qWait(20);
    QCOMPARE(queue.appends, 0);
    QVERIFY(!queue.resumed);
}

void AutoplayServiceTest::oneRequestAtATime()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    queue.current = song(QStringLiteral("seed"));
    autoplay.extensionWanted(false);
    autoplay.extensionWanted(false);
    QCOMPARE(fake.relatedCalls, 1);
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("c")}));
    QTRY_COMPARE(queue.appends, 1);
}

void AutoplayServiceTest::mixWithAutoplayOffAddsOnce()
{
    FakeQueue queue;
    FakeSource fake;
    core::AutoplayService autoplay(queue, fake.source());
    QSignalSpy nothing(&autoplay, &core::AutoplayService::mixFoundNothing);
    // With autoplay on, playing the song is all: the top-up follows.
    autoplay.startMix(song(QStringLiteral("one")));
    QCOMPARE(queue.current->title, QStringLiteral("Song one"));
    QCOMPARE(fake.relatedCalls, 0);
    // With it off, the related songs are added once.
    queue.autoplayOn = false;
    autoplay.startMix(song(QStringLiteral("two")));
    QCOMPARE(fake.relatedCalls, 1);
    FakeSource::answer(fake.relatedPending, songs({QStringLiteral("r")}));
    QTRY_COMPARE(queue.appends, 1);
    // Nothing found says so.
    autoplay.startMix(song(QStringLiteral("three")));
    FakeSource::answer(fake.relatedPending, songs({}));
    QTRY_COMPARE(fake.searchCalls, 1);
    FakeSource::answer(fake.searchPending, songs({}));
    QTRY_COMPARE(nothing.count(), 1);
}

QTEST_GUILESS_MAIN(AutoplayServiceTest)
#include "tst_autoplay_service.moc"
