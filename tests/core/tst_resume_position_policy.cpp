#include <QObject>
#include <QTest>

#include "core/playback/ResumePositionPolicy.h"

using lmp::core::canResumeFrom;
using lmp::core::resumePositionToStore;

class ResumePositionPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void resumePositionPolicy();
    void marginBoundaries();
};

void ResumePositionPolicyTest::resumePositionPolicy()
{
    QCOMPARE(resumePositionToStore(5.0, 300.0), 0.0);
    QCOMPARE(resumePositionToStore(120.0, 300.0), 120.0);
    QCOMPARE(resumePositionToStore(295.0, 300.0), 0.0);
    QCOMPARE(resumePositionToStore(120.0, 0.0), 120.0);

    QVERIFY(!canResumeFrom(0.0, 300.0));
    QVERIFY(canResumeFrom(120.0, 300.0));
    QVERIFY(!canResumeFrom(120.0, 130.0));
}

void ResumePositionPolicyTest::marginBoundaries()
{
    // Exactly 30 s in, and exactly 30 s before the end, still count.
    QCOMPARE(resumePositionToStore(30.0, 300.0), 30.0);
    QCOMPARE(resumePositionToStore(270.0, 300.0), 270.0);
    QCOMPARE(resumePositionToStore(270.5, 300.0), 0.0);
    QCOMPARE(resumePositionToStore(29.9, 0.0), 0.0);
    QVERIFY(canResumeFrom(30.0, 300.0));
    QVERIFY(canResumeFrom(270.0, 300.0));
    QVERIFY(!canResumeFrom(270.5, 300.0));
    QVERIFY(canResumeFrom(5000.0, 0.0));
    QVERIFY(canResumeFrom(5000.0, -1.0));
    static_assert(resumePositionToStore(60.0, 0.0) == 60.0);
}

QTEST_GUILESS_MAIN(ResumePositionPolicyTest)
#include "tst_resume_position_policy.moc"
