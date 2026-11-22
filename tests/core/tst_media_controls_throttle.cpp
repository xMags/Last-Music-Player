#include <QObject>
#include <QTest>

#include "FakePlaybackClock.h"
#include "core/playback/MediaControlsThrottle.h"

using lmp::core::MediaControlsThrottle;

// The progress sent to the system's media controls: at most every 5 s,
// except for forced reports (song or play state changed).
class MediaControlsThrottleTest : public QObject {
    Q_OBJECT

private slots:
    void theFirstReportGoesOut();
    void reportsWithinFiveSecondsAreHeldBack();
    void aReportGoesOutOnceFiveSecondsPassed();
    void forcedReportsAlwaysGoOutAndRestartTheInterval();
};

void MediaControlsThrottleTest::theFirstReportGoesOut()
{
    FakePlaybackClock clock;
    MediaControlsThrottle throttle(clock);
    QVERIFY(throttle.admit(false));
}

void MediaControlsThrottleTest::reportsWithinFiveSecondsAreHeldBack()
{
    FakePlaybackClock clock;
    MediaControlsThrottle throttle(clock);
    QVERIFY(throttle.admit(false));
    clock.advance(500);
    QVERIFY(!throttle.admit(false));
    clock.advance(4499); // 4999 ms after the report
    QVERIFY(!throttle.admit(false));
}

void MediaControlsThrottleTest::aReportGoesOutOnceFiveSecondsPassed()
{
    FakePlaybackClock clock;
    MediaControlsThrottle throttle(clock);
    QVERIFY(throttle.admit(false));
    clock.advance(5000);
    QVERIFY(throttle.admit(false));
    // The interval counts from the report that went out.
    clock.advance(4000);
    QVERIFY(!throttle.admit(false));
    clock.advance(1000);
    QVERIFY(throttle.admit(false));
}

void MediaControlsThrottleTest::forcedReportsAlwaysGoOutAndRestartTheInterval()
{
    FakePlaybackClock clock;
    MediaControlsThrottle throttle(clock);
    QVERIFY(throttle.admit(false));
    clock.advance(100);
    QVERIFY(throttle.admit(true));
    QVERIFY(throttle.admit(true));
    // Five seconds from the forced report, not from the first one.
    clock.advance(4950);
    QVERIFY(!throttle.admit(false));
    clock.advance(50);
    QVERIFY(throttle.admit(false));
}

QTEST_GUILESS_MAIN(MediaControlsThrottleTest)

#include "tst_media_controls_throttle.moc"
