#include "core/lyrics/LyricsService.h"
#include "core/lyrics/LrcText.h"

#include <QtTest/QtTest>

#include "CatalogFixtures.h"

namespace lyrics = lmp::core::lyrics;
using lmp::test::json;

namespace {

lmp::core::Track song(const QString& title, const QString& sourceUrl = {})
{
    lmp::core::Track track;
    track.title = title;
    track.artist = QStringLiteral("Some Artist");
    track.album = QStringLiteral("An Album");
    track.durationSeconds = 201.4;
    track.sourceUrl = sourceUrl;
    return track;
}

// A fetch that answers at once and counts its calls.
struct FakeFetch {
    int calls = 0;
    QList<lyrics::LyricsQuery> queries;
    bool fail = false;
    QByteArray payload = json("{'found':true,'plain':'la la'}");
};

} // namespace

class LyricsServiceTest : public QObject {
    Q_OBJECT

private slots:
    void parsesAndSortsSyncedLines();
    void malformedInputIsEmpty();
    void activeLineFollowsPosition();
    void queryNeverSendsLocalPaths();
    void answersAreKeptPerScope();
    void failuresAreNotKept();
    void cacheKeepsTheLast32();
    void noScopeIsUnavailable();
    void lrcTextRoundTrips();
    void lrcReadsEveryTimeTagAndSkipsTheRest();
    void savedTextIsSyncedOrPlain();
};

void LyricsServiceTest::parsesAndSortsSyncedLines()
{
    const lyrics::LyricsResult result = lyrics::parseLyrics(
        json("{'found':true,'instrumental':false,'source':'s','plain':'p',"
             "'synced':[{'timeMs':3000,'text':'three'},{'timeMs':1000,'text':'one'},'junk',{'text':'zero'}]}"));
    QVERIFY(result.found);
    QCOMPARE(result.plain, QStringLiteral("p"));
    QCOMPARE(result.synced.size(), 3);
    QCOMPARE(result.synced.at(0).text, QStringLiteral("zero"));
    QCOMPARE(result.synced.at(0).timeMs, 0);
    QCOMPARE(result.synced.at(1).text, QStringLiteral("one"));
    QCOMPARE(result.synced.at(2).timeMs, 3000);
    QVERIFY(lyrics::parseLyrics(json("{'instrumental':true}")).instrumental);
}

void LyricsServiceTest::malformedInputIsEmpty()
{
    for (const QByteArray& payload : {QByteArray(), QByteArray("not json"), QByteArray("[1,2]")}) {
        const lyrics::LyricsResult result = lyrics::parseLyrics(payload);
        QVERIFY(!result.found);
        QVERIFY(result.synced.isEmpty());
        QVERIFY(result.plain.isEmpty());
    }
}

void LyricsServiceTest::activeLineFollowsPosition()
{
    const QList<lyrics::LyricLine> lines{{1000, QStringLiteral("a")}, {2000, QStringLiteral("b")},
                                         {2000, QStringLiteral("c")}, {5000, QStringLiteral("d")}};
    QCOMPARE(lyrics::activeLineIndex({}, 1000), -1);
    QCOMPARE(lyrics::activeLineIndex(lines, 999), -1);
    QCOMPARE(lyrics::activeLineIndex(lines, 1000), 0);
    QCOMPARE(lyrics::activeLineIndex(lines, 1999), 0);
    QCOMPARE(lyrics::activeLineIndex(lines, 2000), 2);
    QCOMPARE(lyrics::activeLineIndex(lines, 4999), 2);
    QCOMPARE(lyrics::activeLineIndex(lines, 99999), 3);
}

void LyricsServiceTest::queryNeverSendsLocalPaths()
{
    const lyrics::LyricsQuery local = lyrics::lyricsQueryFor(song(QStringLiteral("One"), QStringLiteral("C:/Music/one.mp3")));
    QVERIFY(local.sourceUrl.isEmpty());
    QCOMPARE(local.durationMs, 201400);
    const QString localPath = lyrics::lyricsPathAndQuery(local);
    QVERIFY(!localPath.contains(QStringLiteral("source")));
    QVERIFY(localPath.startsWith(QStringLiteral("/v1/music/lyrics?artist=Some%20Artist&title=One&album=An%20Album&durationMs=201400")));

    lmp::core::Track remote = song(QStringLiteral("Two & Three"), QStringLiteral("https://music.example.test/watch?v=1"));
    QVERIFY(lyrics::lyricsPathAndQuery(lyrics::lyricsQueryFor(remote))
                .endsWith(QStringLiteral("&sourceUrl=https%3A%2F%2Fmusic.example.test%2Fwatch%3Fv%3D1")));
    QVERIFY(lyrics::lyricsPathAndQuery(lyrics::lyricsQueryFor(remote)).contains(QStringLiteral("title=Two%20%26%20Three")));
    // The song's reference stands in for its link.
    remote.sourceReference = QStringLiteral("ref-1");
    const QString referenced = lyrics::lyricsPathAndQuery(lyrics::lyricsQueryFor(remote));
    QVERIFY(referenced.endsWith(QStringLiteral("&sourceRef=ref-1")));
    QVERIFY(!referenced.contains(QStringLiteral("sourceUrl")));
}

void LyricsServiceTest::answersAreKeptPerScope()
{
    FakeFetch fake;
    QString scope = QStringLiteral("account:a");
    lyrics::LyricsService service(
        [&fake](const lyrics::LyricsQuery& query) {
            ++fake.calls;
            fake.queries.append(query);
            return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(fake.payload));
        },
        [&scope]() { return scope; });
    const lmp::core::Track one = song(QStringLiteral("One"), QStringLiteral("https://music.example.test/1"));
    auto first = service.lyricsFor(one);
    first.waitForFinished();
    QVERIFY(first.result());
    QCOMPARE(first.result().value().plain, QStringLiteral("la la"));
    service.lyricsFor(one).waitForFinished();
    QCOMPARE(fake.calls, 1);
    // Another account asks for itself.
    scope = QStringLiteral("account:b");
    service.lyricsFor(one).waitForFinished();
    QCOMPARE(fake.calls, 2);
    // Without a link, the key is artist, title and whole seconds, in any case.
    lmp::core::Track bare = song(QStringLiteral("Bare"));
    service.lyricsFor(bare).waitForFinished();
    bare.title = QStringLiteral("BARE");
    bare.durationSeconds = 201.9;
    service.lyricsFor(bare).waitForFinished();
    QCOMPARE(fake.calls, 3);
    service.clear();
    service.lyricsFor(bare).waitForFinished();
    QCOMPARE(fake.calls, 4);
}

void LyricsServiceTest::failuresAreNotKept()
{
    FakeFetch fake;
    lyrics::LyricsService service(
        [&fake](const lyrics::LyricsQuery&) {
            ++fake.calls;
            if (fake.fail) {
                return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(
                    lmp::core::Failure::of(lmp::core::FailureKind::Network, QStringLiteral("offline"))));
            }
            return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(fake.payload));
        },
        []() { return QStringLiteral("account:a"); });
    const lmp::core::Track one = song(QStringLiteral("One"));
    fake.fail = true;
    auto failed = service.lyricsFor(one);
    failed.waitForFinished();
    QVERIFY(!failed.result());
    fake.fail = false;
    fake.payload = QByteArray("not json");
    auto unreadable = service.lyricsFor(one);
    unreadable.waitForFinished();
    QVERIFY(!unreadable.result());
    fake.payload = json("{'found':false}");
    service.lyricsFor(one).waitForFinished();
    service.lyricsFor(one).waitForFinished();
    QCOMPARE(fake.calls, 3);
}

void LyricsServiceTest::cacheKeepsTheLast32()
{
    FakeFetch fake;
    lyrics::LyricsService service(
        [&fake](const lyrics::LyricsQuery&) {
            ++fake.calls;
            return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(fake.payload));
        },
        []() { return QStringLiteral("account:a"); });
    for (int index = 0; index <= static_cast<int>(lyrics::LyricsService::CacheCapacity); ++index) {
        service.lyricsFor(song(QStringLiteral("Song %1").arg(index))).waitForFinished();
    }
    QCOMPARE(fake.calls, 33);
    // The newest is kept; the oldest went.
    service.lyricsFor(song(QStringLiteral("Song 32"))).waitForFinished();
    QCOMPARE(fake.calls, 33);
    service.lyricsFor(song(QStringLiteral("Song 0"))).waitForFinished();
    QCOMPARE(fake.calls, 34);
}

void LyricsServiceTest::noScopeIsUnavailable()
{
    int calls = 0;
    lyrics::LyricsService service(
        [&calls](const lyrics::LyricsQuery&) {
            ++calls;
            return QtFuture::makeReadyValueFuture(lmp::core::Result<QByteArray>(QByteArray("{}")));
        },
        []() { return QString(); });
    QVERIFY(!service.available());
    auto answer = service.lyricsFor(song(QStringLiteral("One")));
    answer.waitForFinished();
    QVERIFY(!answer.result());
    QCOMPARE(calls, 0);
}

void LyricsServiceTest::lrcTextRoundTrips()
{
    const QList<lyrics::LyricLine> lines{{0, QStringLiteral("first")}, {61230, QStringLiteral("two\nlines")},
                                         {6000000, QStringLiteral("late")}};
    const QString text = lyrics::formatLrc(lines);
    QCOMPARE(text, QStringLiteral("[00:00.00]first\n[01:01.23]two lines\n[100:00.00]late\n"));
    const QList<lyrics::LyricLine> read = lyrics::parseLrc(text);
    QCOMPARE(read.size(), 3);
    QCOMPARE(read.at(1).timeMs, 61230);
    QCOMPARE(read.at(1).text, QStringLiteral("two lines"));
    QCOMPARE(read.at(2).timeMs, 6000000);
    QVERIFY(lyrics::formatLrc({}).isEmpty());
}

void LyricsServiceTest::lrcReadsEveryTimeTagAndSkipsTheRest()
{
    const QList<lyrics::LyricLine> read = lyrics::parseLrc(
        QStringLiteral("[ar:Someone]\r\n[00:05.5]half\r\n[00:02][00:09.005]twice\r\nno time\r\n[0x:10]bad\r\n"));
    QCOMPARE(read.size(), 3);
    QCOMPARE(read.at(0).timeMs, 2000);
    QCOMPARE(read.at(0).text, QStringLiteral("twice"));
    QCOMPARE(read.at(1).timeMs, 5500);
    QCOMPARE(read.at(2).timeMs, 9005);
}

void LyricsServiceTest::savedTextIsSyncedOrPlain()
{
    const lyrics::LyricsResult synced = lyrics::lyricsFromSavedText(QStringLiteral("[00:01.00]a\n[00:02.00]b\n"));
    QVERIFY(synced.found);
    QCOMPARE(synced.synced.size(), 2);
    QCOMPARE(synced.plain, QStringLiteral("a\nb"));
    const lyrics::LyricsResult plain = lyrics::lyricsFromSavedText(QStringLiteral("  just words\n"));
    QVERIFY(plain.found);
    QVERIFY(plain.synced.isEmpty());
    QCOMPARE(plain.plain, QStringLiteral("just words"));
    QVERIFY(!lyrics::lyricsFromSavedText(QStringLiteral(" \n ")).found);
}

QTEST_GUILESS_MAIN(LyricsServiceTest)
#include "tst_lyrics_service.moc"
