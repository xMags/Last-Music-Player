#include <QObject>
#include <QTest>

#include <optional>

#include "core/app/StallWatch.h"

using lmp::core::StallRule;

// The GUI stall watcher's timing rule, with the times given by the test.
class StallRuleTest : public QObject {
    Q_OBJECT

private slots:
    void beatsInTimeAreNoStall();
    void aStallIsReportedOnceWhenTheLoopTurnsAgain();
    void aStillBlockedLoopIsNotReportedYet();
};

void StallRuleTest::beatsInTimeAreNoStall()
{
    StallRule rule(100);
    for (qint64 now = 0; now < 1000; now += 20) {
        // The loop beats every 16 ms; the check comes a little later.
        QVERIFY(!rule.check(now - 4, now).has_value());
    }
    // Exactly at the threshold is not over it.
    QVERIFY(!rule.check(900, 1000).has_value());
}

void StallRuleTest::aStallIsReportedOnceWhenTheLoopTurnsAgain()
{
    StallRule rule(100);
    QVERIFY(!rule.check(1000, 1020).has_value());
    // No beat since 1000: over the threshold at 1120, noted, not reported.
    QVERIFY(!rule.check(1000, 1120).has_value());
    QVERIFY(!rule.check(1000, 1300).has_value());
    // The loop turns at 1350: from the beat before to the beat after.
    const std::optional<qint64> stall = rule.check(1350, 1360);
    QVERIFY(stall.has_value());
    QCOMPARE(*stall, 350);
    // Once only.
    QVERIFY(!rule.check(1366, 1380).has_value());
    // A second stall is its own.
    QVERIFY(!rule.check(1366, 1500).has_value());
    QCOMPARE(rule.check(1620, 1630).value_or(-1), 254);
}

void StallRuleTest::aStillBlockedLoopIsNotReportedYet()
{
    StallRule rule(100);
    QVERIFY(!rule.check(0, 150).has_value());
    for (qint64 now = 170; now < 5000; now += 20) {
        QVERIFY(!rule.check(0, now).has_value());
    }
}

QTEST_GUILESS_MAIN(StallRuleTest)
#include "tst_stall_rule.moc"
