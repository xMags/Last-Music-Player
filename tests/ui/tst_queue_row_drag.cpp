#include <QGuiApplication>
#include <QObject>
#include <QPointF>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>

#include "ui/player/QueueRowDrag.h"

using lmp::ui::QueueRowDrag;

namespace {

constexpr qreal kRow = 56;

// A list of five rows in a 200px viewport scrolled to the top.
void layOut(QueueRowDrag& drag)
{
    drag.setRowHeight(kRow);
    drag.setRowCount(5);
    drag.setViewportHeight(200);
}

// A press in the middle of `row`, with the rows starting at 0.
void pressRow(QueueRowDrag& drag, int row)
{
    const qreal y = row * kRow + kRow / 2;
    drag.press(row, QStringLiteral("key-%1").arg(row), row * kRow, QPointF(40, y), y);
}

} // namespace

// The Up Next row gesture: tap, lift by moving or holding, drop slot and
// edge scrolling, with the reference thresholds.
class QueueRowDragTest : public QObject {
    Q_OBJECT

private slots:
    void thresholdsMatchReference();
    void aPressThatStaysPutIsATap();
    void theSecondPressOfADoubleClickIsNoTap();
    void movingUpOrDownPastTheSlopLiftsTheRow();
    void aSidewaysStartDoesNothing();
    void holdingLiftsTheRowWithoutMovingIt();
    void dropSlotSkipsTheLiftedRow();
    void nearAnEdgeTheListScrolls();
    void aListChangeCallsTheDragOff();
};

void QueueRowDragTest::thresholdsMatchReference()
{
    QCOMPARE(QueueRowDrag::SlopPixels, 8.0);
    QCOMPARE(QueueRowDrag::AutoScrollEdge, 36.0);
    QCOMPARE(QueueRowDrag::AutoScrollStep, 8);
    QCOMPARE(QueueRowDrag::HoldDelay.count(), 220);
}

void QueueRowDragTest::aPressThatStaysPutIsATap()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy tapped(&drag, &QueueRowDrag::tapped);
    QSignalSpy dropped(&drag, &QueueRowDrag::dropped);
    pressRow(drag, 2);
    // Under the slop: still a press.
    drag.move(QPointF(43, 2 * kRow + kRow / 2 + 4), 120);
    QVERIFY(!drag.dragging());
    drag.release();
    QCOMPARE(tapped.count(), 1);
    QCOMPARE(tapped.at(0).at(0).toInt(), 2);
    QCOMPARE(tapped.at(0).at(1).toString(), QStringLiteral("key-2"));
    QCOMPARE(dropped.count(), 0);
}

void QueueRowDragTest::theSecondPressOfADoubleClickIsNoTap()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy tapped(&drag, &QueueRowDrag::tapped);
    pressRow(drag, 1);
    drag.release();
    pressRow(drag, 1);
    drag.release();
    QCOMPARE(tapped.count(), 1);
    QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
    pressRow(drag, 1);
    drag.release();
    QCOMPARE(tapped.count(), 2);
}

void QueueRowDragTest::movingUpOrDownPastTheSlopLiftsTheRow()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy tapped(&drag, &QueueRowDrag::tapped);
    QSignalSpy dropped(&drag, &QueueRowDrag::dropped);
    pressRow(drag, 1);
    const qreal start = kRow + kRow / 2;
    drag.move(QPointF(40, start + 8), 100);
    QVERIFY(drag.dragging());
    QCOMPARE(drag.liftedRow(), 1);
    QCOMPARE(drag.liftOffset(), 8.0);

    // Over the middle of row 3: the row lands after it.
    drag.move(QPointF(40, 3 * kRow + kRow / 2 + 1), 100);
    QCOMPARE(drag.targetRow(), 3);
    QCOMPARE(drag.liftOffset(), 2 * kRow + 1);
    drag.release();
    QVERIFY(!drag.dragging());
    QCOMPARE(drag.liftedRow(), -1);
    QCOMPARE(tapped.count(), 0);
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.at(0).at(0).toInt(), 1);
    QCOMPARE(dropped.at(0).at(1).toInt(), 3);
    QCOMPARE(dropped.at(0).at(2).toString(), QStringLiteral("key-1"));

    // Carried back to where it started: nothing is reported.
    pressRow(drag, 1);
    drag.move(QPointF(40, start - 20), 100);
    drag.move(QPointF(40, start), 100);
    drag.release();
    QCOMPARE(dropped.count(), 1);
}

void QueueRowDragTest::aSidewaysStartDoesNothing()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy tapped(&drag, &QueueRowDrag::tapped);
    QSignalSpy dropped(&drag, &QueueRowDrag::dropped);
    pressRow(drag, 0);
    drag.move(QPointF(40 + 10, kRow / 2 + 3), 30);
    QVERIFY(!drag.dragging());
    drag.release();
    QCOMPARE(tapped.count(), 0);
    QCOMPARE(dropped.count(), 0);
}

void QueueRowDragTest::holdingLiftsTheRowWithoutMovingIt()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy tapped(&drag, &QueueRowDrag::tapped);
    QSignalSpy dropped(&drag, &QueueRowDrag::dropped);
    pressRow(drag, 2);
    QTRY_VERIFY_WITH_TIMEOUT(drag.dragging(), 1000);
    QCOMPARE(drag.liftedRow(), 2);
    // A held row let go where it is does nothing, not even a tap.
    drag.release();
    QCOMPARE(tapped.count(), 0);
    QCOMPARE(dropped.count(), 0);
}

void QueueRowDragTest::dropSlotSkipsTheLiftedRow()
{
    // Five rows of 10px; the row lifted from 1.
    QCOMPARE(QueueRowDrag::dropSlot(1, 2, 10, 5), 0);   // above row 0's middle
    QCOMPARE(QueueRowDrag::dropSlot(1, 12, 10, 5), 1);  // over itself: below row 0's middle
    QCOMPARE(QueueRowDrag::dropSlot(1, 24, 10, 5), 1);  // above row 2's middle
    QCOMPARE(QueueRowDrag::dropSlot(1, 26, 10, 5), 2);  // past row 2's middle
    QCOMPARE(QueueRowDrag::dropSlot(1, 90, 10, 5), 4);  // below every row: the last slot
    QCOMPARE(QueueRowDrag::dropSlot(3, 26, 10, 5), 3);  // lifted from 3, past row 2's middle
    QCOMPARE(QueueRowDrag::dropSlot(0, 0, 10, 0), 0);
}

void QueueRowDragTest::nearAnEdgeTheListScrolls()
{
    QueueRowDrag drag;
    layOut(drag);
    pressRow(drag, 1);
    const qreal start = kRow + kRow / 2;
    drag.move(QPointF(40, start + 10), 100);
    QCOMPARE(drag.scrollStep(), 0);
    // Within 36px of the bottom.
    drag.move(QPointF(40, start + 80), 200 - 30);
    QCOMPARE(drag.scrollStep(), 8);
    const int target = drag.targetRow();
    qreal contentY = 0;
    for (int frame = 0; frame < 20; ++frame) {
        contentY = drag.scrolledContentY(contentY, 0, 100);
    }
    // Clamped at the end; the pointer moved with the content.
    QCOMPARE(contentY, 100.0);
    QCOMPARE(drag.liftOffset(), 80.0 + 100.0);
    QVERIFY(drag.targetRow() > target);

    // Within 36px of the top.
    drag.move(QPointF(40, start + 80), 20);
    QCOMPARE(drag.scrollStep(), -8);
    QCOMPARE(drag.scrolledContentY(100, 0, 100), 92.0);
    drag.release();
    QCOMPARE(drag.scrollStep(), 0);
    QCOMPARE(drag.scrolledContentY(50, 0, 100), 50.0);
}

void QueueRowDragTest::aListChangeCallsTheDragOff()
{
    QueueRowDrag drag;
    layOut(drag);
    QSignalSpy dropped(&drag, &QueueRowDrag::dropped);
    pressRow(drag, 1);
    drag.move(QPointF(40, kRow + kRow / 2 + 60), 100);
    QVERIFY(drag.dragging());
    drag.setRowCount(4);
    QVERIFY(!drag.dragging());
    drag.release();
    QCOMPARE(dropped.count(), 0);
}

QTEST_MAIN(QueueRowDragTest)
#include "tst_queue_row_drag.moc"
