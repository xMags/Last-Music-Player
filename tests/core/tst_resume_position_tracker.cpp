#include <QObject>
#include <QString>
#include <QTest>

#include <memory>

#include "TempLibrary.h"
#include "core/db/TrackRepository.h"
#include "core/model/Track.h"
#include "core/playback/LastTrackIdentity.h"
#include "core/playback/ResumePositionTracker.h"

using lmp::core::ResumePositionTracker;
using lmp::core::Track;

namespace {

Track song(const QString& name)
{
    Track track;
    track.title = name;
    track.artist = QStringLiteral("Artist");
    track.sourceKind = QStringLiteral("local");
    track.filePath = QStringLiteral("C:\\Music\\") + name + QStringLiteral(".mp3");
    track.durationSeconds = 300.0;
    return track;
}

} // namespace

class ResumePositionTrackerTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void storedOnlyThirtySecondsIn();
    void storedOnlyWithThirtySecondsLeft();
    void unknownDurationChecksOnlyTheHead();
    void periodicSaveWaitsForTenSeconds();
    void forcedSaveWritesTheExactPoint();
    void clearedAtTheEnd();
    void zeroIsNotWrittenOverZero();
    void eachSongKeepsItsOwnPoint();

private:
    [[nodiscard]] double stored(const Track& track);
    void store(const Track& track, double seconds);

    std::unique_ptr<TempLibrary> m_library;
    std::unique_ptr<lmp::core::db::TrackRepository> m_tracks;
    std::unique_ptr<ResumePositionTracker> m_tracker;
    Track m_song;
};

void ResumePositionTrackerTest::init()
{
    m_library = std::make_unique<TempLibrary>();
    QVERIFY(m_library->ok());
    m_tracks = std::make_unique<lmp::core::db::TrackRepository>(m_library->database());
    m_tracker = std::make_unique<ResumePositionTracker>(*m_tracks);
    m_song = song(QStringLiteral("One"));
    QVERIFY(m_tracks->upsertLocalTrack(m_song, lmp::core::legacyCatalogSourceKey(m_song)).isOk());
}

void ResumePositionTrackerTest::cleanup()
{
    m_tracker.reset();
    m_tracks.reset();
    m_library.reset();
}

double ResumePositionTrackerTest::stored(const Track& track)
{
    auto statement = m_library->connection().prepare("SELECT LastPositionSeconds FROM Tracks WHERE SourceKey=?;");
    if (!statement) {
        return -1.0;
    }
    statement.value().bindText(1, lmp::core::legacyCatalogSourceKey(track));
    if (!statement.value().step().valueOr(false)) {
        return -1.0;
    }
    return statement.value().columnDouble(0);
}

void ResumePositionTrackerTest::store(const Track& track, double seconds)
{
    QVERIFY(m_tracks->recordPlaybackPosition(lmp::core::legacyCatalogSourceKey(track), seconds).isOk());
}

void ResumePositionTrackerTest::storedOnlyThirtySecondsIn()
{
    store(m_song, 120.0);
    m_tracker->observe(m_song, 29.0, 300.0);
    QCOMPARE(stored(m_song), 0.0); // a new song: its point is cleared
    m_tracker->saveNow(m_song, 30.0, 300.0);
    QCOMPARE(stored(m_song), 30.0);
}

void ResumePositionTrackerTest::storedOnlyWithThirtySecondsLeft()
{
    m_tracker->observe(m_song, 270.0, 300.0);
    QCOMPARE(stored(m_song), 270.0);
    m_tracker->observe(m_song, 280.0, 300.0);
    QCOMPARE(stored(m_song), 0.0);
}

void ResumePositionTrackerTest::unknownDurationChecksOnlyTheHead()
{
    m_tracker->observe(m_song, 290.0, 0.0);
    QCOMPARE(stored(m_song), 290.0);
}

void ResumePositionTrackerTest::periodicSaveWaitsForTenSeconds()
{
    m_tracker->observe(m_song, 60.0, 300.0);
    QCOMPARE(stored(m_song), 60.0);
    m_tracker->observe(m_song, 69.9, 300.0);
    QCOMPARE(stored(m_song), 60.0);
    m_tracker->observe(m_song, 70.0, 300.0);
    QCOMPARE(stored(m_song), 70.0);
    // Backwards counts the same way.
    m_tracker->observe(m_song, 61.0, 300.0);
    QCOMPARE(stored(m_song), 70.0);
    m_tracker->observe(m_song, 60.0, 300.0);
    QCOMPARE(stored(m_song), 60.0);
}

void ResumePositionTrackerTest::forcedSaveWritesTheExactPoint()
{
    m_tracker->observe(m_song, 60.0, 300.0);
    m_tracker->saveNow(m_song, 63.5, 300.0);
    QCOMPARE(stored(m_song), 63.5);
    // Forced still follows the margins.
    m_tracker->saveNow(m_song, 290.0, 300.0);
    QCOMPARE(stored(m_song), 0.0);
}

void ResumePositionTrackerTest::clearedAtTheEnd()
{
    m_tracker->observe(m_song, 100.0, 300.0);
    QCOMPARE(stored(m_song), 100.0);
    m_tracker->clear(m_song);
    QCOMPARE(stored(m_song), 0.0);
}

void ResumePositionTrackerTest::zeroIsNotWrittenOverZero()
{
    m_tracker->observe(m_song, 5.0, 300.0); // writes 0 for the new song
    store(m_song, 50.0);                    // changed behind the tracker's back
    m_tracker->observe(m_song, 6.0, 300.0);
    QCOMPARE(stored(m_song), 50.0); // nothing written
    m_tracker->saveNow(m_song, 7.0, 300.0);
    QCOMPARE(stored(m_song), 50.0); // not even when forced
}

void ResumePositionTrackerTest::eachSongKeepsItsOwnPoint()
{
    const Track other = song(QStringLiteral("Two"));
    QVERIFY(m_tracks->upsertLocalTrack(other, lmp::core::legacyCatalogSourceKey(other)).isOk());
    m_tracker->observe(m_song, 100.0, 300.0);
    m_tracker->observe(other, 105.0, 300.0); // another song: written at once
    QCOMPARE(stored(other), 105.0);
    QCOMPARE(stored(m_song), 100.0);
}

QTEST_GUILESS_MAIN(ResumePositionTrackerTest)
#include "tst_resume_position_tracker.moc"
