#include <QEasingCurve>
#include <QSignalSpy>
#include <QTest>

#include "ui/motion/AmbientBounce.h"
#include "ui/motion/AmbientDrift.h"
#include "ui/motion/AmbientPhase.h"
#include "ui/theme/Motion.h"

#include <cmath>

using lmp::ui::AmbientBounce;
using lmp::ui::AmbientDrift;
using lmp::ui::AmbientPhase;
using lmp::ui::DriftPose;

namespace {

constexpr qint64 NsPerMs = 1000000;

// A clock the test moves by hand, in milliseconds.
struct ManualClock {
    qint64 ms = 0;

    void attach(AmbientPhase& phase)
    {
        phase.setClock([this]() { return ms * NsPerMs; });
    }
};

void comparePose(const DriftPose& actual, const DriftPose& expected)
{
    QVERIFY2(std::abs(actual.x - expected.x) < 1e-9, qPrintable(QString::number(actual.x)));
    QVERIFY2(std::abs(actual.y - expected.y) < 1e-9, qPrintable(QString::number(actual.y)));
    QVERIFY2(std::abs(actual.scale - expected.scale) < 1e-9, qPrintable(QString::number(actual.scale)));
}

} // namespace

// The clock of the endless ambient motions, and what the bouncing bars and
// the drifting glow show at each moment.
class AmbientMotionTest : public QObject {
    Q_OBJECT

private slots:
    void theTimerRunsOnlyWhileRunningWithAPeriod()
    {
        AmbientPhase phase;
        QVERIFY(!phase.stepping());
        phase.setRunning(true);
        // No period yet: nothing to step.
        QVERIFY(!phase.stepping());
        QVERIFY(!phase.active());
        phase.setPeriod(1000);
        QVERIFY(phase.stepping());
        QVERIFY(phase.active());
        phase.setRunning(false);
        QVERIFY(!phase.stepping());
        QVERIFY(!phase.active());
    }

    void thePhaseFollowsElapsedTimeAndWraps()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(1000);
        phase.setRunning(true);
        QSignalSpy changes(&phase, &AmbientPhase::phaseChanged);

        clock.ms = 250;
        phase.step();
        QCOMPARE(phase.phase(), 0.25);
        // A late step lands where real time says, not one step further.
        clock.ms = 1100;
        phase.step();
        QCOMPARE(phase.elapsed(), 1100.0);
        QVERIFY(std::abs(phase.phase() - 0.1) < 1e-9);
        QCOMPARE(changes.count(), 2);
        // Without time passing nothing changes.
        phase.step();
        QCOMPARE(changes.count(), 2);
    }

    void aStoppedPhaseHoldsAndNoStepMovesIt()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(1000);
        phase.setRunning(true);
        clock.ms = 400;
        phase.step();
        phase.setRunning(false);
        clock.ms = 900;
        phase.step();
        QCOMPARE(phase.phase(), 0.4);
    }

    void aRestartBeginsAtZero()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(1000);
        phase.setRunning(true);
        clock.ms = 400;
        phase.step();
        phase.setRunning(false);

        // The reset is announced before the start, while still inactive.
        bool activeAtReset = true;
        connect(&phase, &AmbientPhase::phaseChanged, this, [&phase, &activeAtReset]() {
            activeAtReset = phase.active();
        });
        clock.ms = 5000;
        phase.setRunning(true);
        QVERIFY(!activeAtReset);
        QCOMPARE(phase.phase(), 0.0);
        clock.ms = 5100;
        phase.step();
        QCOMPARE(phase.phase(), 0.1);
    }

    void aResumingPhaseGoesOnWhereItStopped()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setResume(true);
        phase.setPeriod(1000);
        phase.setRunning(true);
        clock.ms = 400;
        phase.step();
        phase.setRunning(false);
        // The pause does not count.
        clock.ms = 7000;
        phase.setRunning(true);
        QCOMPARE(phase.phase(), 0.4);
        clock.ms = 7300;
        phase.step();
        QVERIFY(std::abs(phase.phase() - 0.7) < 1e-9);
    }

    void aHeldPhaseFreezesAndGoesOnWhereItWasHeld()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(1000);
        phase.setRunning(true);
        clock.ms = 300;
        phase.step();

        QSignalSpy activeChanges(&phase, &AmbientPhase::activeChanged);
        phase.setHeld(true);
        // Still under way, but nothing steps it.
        QVERIFY(phase.active());
        QVERIFY(!phase.stepping());
        clock.ms = 5000;
        phase.step();
        QCOMPARE(phase.phase(), 0.3);

        // Released, it neither restarts nor jumps by the held time, even
        // without `resume`.
        phase.setHeld(false);
        QVERIFY(phase.stepping());
        QCOMPARE(phase.phase(), 0.3);
        clock.ms = 5200;
        phase.step();
        QVERIFY(std::abs(phase.phase() - 0.5) < 1e-9);
        QCOMPARE(activeChanges.count(), 0);
    }

    void aPhaseStartedWhileHeldWaitsForTheRelease()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(1000);
        phase.setHeld(true);
        phase.setRunning(true);
        QVERIFY(phase.active());
        QVERIFY(!phase.stepping());
        clock.ms = 800;
        phase.setHeld(false);
        QCOMPARE(phase.phase(), 0.0);
        clock.ms = 900;
        phase.step();
        QVERIFY(std::abs(phase.phase() - 0.1) < 1e-9);
        // Stopping while held still stops.
        phase.setHeld(true);
        phase.setRunning(false);
        QVERIFY(!phase.active());
        phase.setHeld(false);
        QVERIFY(!phase.stepping());
    }

    void aBounceHoldsDuringItsDelayThenGoesUpAndDown()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(900);
        AmbientBounce bounce;
        bounce.setClock(&phase);
        bounce.setDelay(200);
        bounce.setHalfPeriod(450);
        bounce.setFrom(0.4);
        bounce.setTo(1);
        const QEasingCurve quad(QEasingCurve::InOutQuad);
        bounce.setEasing(quad);
        QVERIFY(bounce.holding());

        phase.setRunning(true);
        QVERIFY(bounce.holding());
        clock.ms = 199;
        phase.step();
        QVERIFY(bounce.holding());

        QSignalSpy values(&bounce, &AmbientBounce::valueChanged);
        clock.ms = 200;
        phase.step();
        QVERIFY(!bounce.holding());
        QCOMPARE(bounce.value(), 0.4);
        QCOMPARE(values.count(), 1);

        clock.ms = 200 + 225;
        phase.step();
        QCOMPARE(bounce.value(), 0.4 + 0.6 * quad.valueForProgress(0.5));
        clock.ms = 200 + 450;
        phase.step();
        QCOMPARE(bounce.value(), 1.0);
        clock.ms = 200 + 450 + 100;
        phase.step();
        QCOMPARE(bounce.value(), 1.0 - 0.6 * quad.valueForProgress(100.0 / 450));
        // The next bounce, a whole loop later.
        clock.ms = 200 + 900 + 100;
        phase.step();
        QCOMPARE(bounce.value(), 0.4 + 0.6 * quad.valueForProgress(100.0 / 450));

        phase.setRunning(false);
        QVERIFY(bounce.holding());
    }

    void aBounceWithoutDelayStartsAtItsFirstValue()
    {
        AmbientPhase phase;
        phase.setPeriod(900);
        AmbientBounce bounce;
        bounce.setClock(&phase);
        bounce.setHalfPeriod(450);
        bounce.setFrom(1);
        bounce.setTo(0.7);
        QSignalSpy values(&bounce, &AmbientBounce::valueChanged);
        phase.setRunning(true);
        QVERIFY(!bounce.holding());
        QCOMPARE(bounce.value(), 1.0);
        // Announced although the number did not change from its default.
        QCOMPARE(values.count(), 1);
    }

    void theDriftMeetsEveryKeyframeOnTime()
    {
        const QEasingCurve easing = lmp::ui::Motion().easeInOut();
        const DriftPose start = AmbientDrift::start();
        comparePose(start, {-0.14, -270, 1});
        comparePose(AmbientDrift::poseAt(0, start, easing), {-0.14, -270, 1});
        comparePose(AmbientDrift::poseAt(0.175, start, easing), {0.11, -210, 1.12});
        comparePose(AmbientDrift::poseAt(0.35, start, easing), {0.32, -260, 0.94});
        comparePose(AmbientDrift::poseAt(0.5, start, easing), {0.05, -190, 1.06});
        comparePose(AmbientDrift::poseAt(0.65, start, easing), {0.32, -260, 0.94});
        comparePose(AmbientDrift::poseAt(0.825, start, easing), {0.11, -210, 1.12});
        comparePose(AmbientDrift::poseAt(1, start, easing), {-0.14, -270, 1});
    }

    void theDriftIsContinuousAcrossKeyframes()
    {
        const QEasingCurve easing = lmp::ui::Motion().easeInOut();
        const DriftPose start = AmbientDrift::start();
        for (const qreal boundary : {0.175, 0.35, 0.5, 0.65, 0.825}) {
            const DriftPose before = AmbientDrift::poseAt(boundary - 1e-7, start, easing);
            const DriftPose after = AmbientDrift::poseAt(boundary + 1e-7, start, easing);
            QVERIFY2(std::abs(before.x - after.x) < 1e-4, qPrintable(QString::number(boundary)));
            QVERIFY2(std::abs(before.y - after.y) < 1e-2, qPrintable(QString::number(boundary)));
            QVERIFY2(std::abs(before.scale - after.scale) < 1e-4, qPrintable(QString::number(boundary)));
        }
    }

    void theMiddleOfALegFollowsTheEasing()
    {
        const QEasingCurve easing = lmp::ui::Motion().easeInOut();
        const DriftPose start = AmbientDrift::start();
        // A quarter into the third leg (0.35 to 0.5).
        const qreal t = easing.valueForProgress(0.25);
        comparePose(AmbientDrift::poseAt(0.35 + 0.15 * 0.25, start, easing),
                    {0.32 + (0.05 - 0.32) * t, -260 + (-190 + 260) * t, 0.94 + (1.06 - 0.94) * t});
        // Halfway through the first leg.
        const qreal half = easing.valueForProgress(0.5);
        comparePose(AmbientDrift::poseAt(0.0875, start, easing),
                    {-0.14 + 0.25 * half, -270 + 60 * half, 1 + 0.12 * half});
    }

    void aRestartedDriftSetsOffFromWhereItStopped()
    {
        ManualClock clock;
        AmbientPhase phase;
        clock.attach(phase);
        phase.setPeriod(52000);
        AmbientDrift drift;
        drift.setEasing(lmp::ui::Motion().easeInOut());
        drift.setClock(&phase);
        phase.setRunning(true);
        comparePose({drift.x(), drift.y(), drift.scale()}, AmbientDrift::start());

        clock.ms = 4550; // Halfway through the first leg.
        phase.step();
        const DriftPose stopped{drift.x(), drift.y(), drift.scale()};
        phase.setRunning(false);

        clock.ms = 100000;
        phase.setRunning(true);
        // No jump back to the first keyframe.
        comparePose({drift.x(), drift.y(), drift.scale()}, stopped);
        // The first leg then runs from there to the second keyframe.
        clock.ms = 100000 + 9100;
        phase.step();
        comparePose({drift.x(), drift.y(), drift.scale()}, {0.11, -210, 1.12});
        // Later loops start at the first keyframe again.
        clock.ms = 100000 + 52000 + 4550;
        phase.step();
        comparePose({drift.x(), drift.y(), drift.scale()},
                    AmbientDrift::poseAt(4550.0 / 52000, AmbientDrift::start(), drift.easing()));
    }
};

QTEST_GUILESS_MAIN(AmbientMotionTest)
#include "tst_ambient_motion.moc"
