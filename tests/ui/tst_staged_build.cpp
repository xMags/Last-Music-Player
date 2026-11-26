#include <QCoreApplication>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "ui/shell/StagedBuild.h"

using lmp::ui::StagedBuild;

// Parts of a page are served one per turn of the event loop, in the order
// they took their tickets, and never in the turn the tickets were taken.
class StagedBuildTest : public QObject {
    Q_OBJECT

private slots:
    void servesOneTicketPerTurnInOrder();
    void aLaterTicketStartsServingAgain();
};

void StagedBuildTest::servesOneTicketPerTurnInOrder()
{
    StagedBuild stage;
    QCOMPARE(stage.take(), 1);
    QCOMPARE(stage.take(), 2);
    QCOMPARE(stage.take(), 3);
    QCOMPARE(stage.served(), 0);

    QSignalSpy served(&stage, &StagedBuild::servedChanged);
    QCoreApplication::processEvents();
    QCOMPARE(stage.served(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(stage.served(), 2);
    QTRY_COMPARE(stage.served(), 3);
    QCOMPARE(served.count(), 3);

    // Nothing left: serving stops.
    QTest::qWait(20);
    QCOMPARE(stage.served(), 3);
}

void StagedBuildTest::aLaterTicketStartsServingAgain()
{
    StagedBuild stage;
    stage.take();
    QTRY_COMPARE(stage.served(), 1);
    QCOMPARE(stage.take(), 2);
    QCOMPARE(stage.served(), 1);
    QTRY_COMPARE(stage.served(), 2);
}

QTEST_GUILESS_MAIN(StagedBuildTest)
#include "tst_staged_build.moc"
