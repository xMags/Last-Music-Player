#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <chrono>

namespace lmp::ui {

// The mouse gesture of the Up Next rows, one per list: a press that does not
// move is a tap (the row plays); a press held for HoldDelay, or one that
// moves SlopPixels mostly up or down, lifts the row, which then follows the
// pointer until release drops it into the slot under the pointer. Near the
// list's top or bottom edge (AutoScrollEdge) the list scrolls by
// AutoScrollStep each frame while lifted. A sideways start ends the gesture
// with nothing done.
//
// Positions are in the list content's coordinates (they do not change while
// the list scrolls), the viewport position in the list's own. Rows are
// rowHeight tall with nothing between them. Lives on the GUI thread; its
// timer is a child.
class QueueRowDrag : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qreal rowHeight READ rowHeight WRITE setRowHeight NOTIFY layoutChanged FINAL)
    Q_PROPERTY(int rowCount READ rowCount WRITE setRowCount NOTIFY layoutChanged FINAL)
    Q_PROPERTY(qreal viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY layoutChanged FINAL)
    Q_PROPERTY(bool dragging READ dragging NOTIFY dragChanged FINAL)
    // The lifted row and the slot it would land in; -1 while nothing is lifted.
    Q_PROPERTY(int liftedRow READ liftedRow NOTIFY dragChanged FINAL)
    Q_PROPERTY(int targetRow READ targetRow NOTIFY dragChanged FINAL)
    // How far the lifted row has been carried from its place.
    Q_PROPERTY(qreal liftOffset READ liftOffset NOTIFY dragChanged FINAL)
    // The scroll step each frame while lifted: negative up, 0 for none.
    Q_PROPERTY(int scrollStep READ scrollStep NOTIFY dragChanged FINAL)

public:
    static constexpr qreal SlopPixels = 8;
    static constexpr qreal AutoScrollEdge = 36;
    static constexpr int AutoScrollStep = 8;
    static constexpr std::chrono::milliseconds HoldDelay{220};

    explicit QueueRowDrag(QObject* parent = nullptr);

    [[nodiscard]] qreal rowHeight() const { return m_rowHeight; }
    void setRowHeight(qreal height);
    [[nodiscard]] int rowCount() const { return m_rowCount; }
    void setRowCount(int count);
    [[nodiscard]] qreal viewportHeight() const { return m_viewportHeight; }
    void setViewportHeight(qreal height);
    [[nodiscard]] bool dragging() const { return m_mode == Mode::Dragging; }
    [[nodiscard]] int liftedRow() const { return dragging() ? m_row : -1; }
    [[nodiscard]] int targetRow() const { return dragging() ? m_target : -1; }
    [[nodiscard]] qreal liftOffset() const { return dragging() ? m_pointerY - m_startY : 0; }
    [[nodiscard]] int scrollStep() const;

    // A left press on `row` (its model key, and its top in the content) at
    // `point`; `viewportY` is the same point in the list's own coordinates.
    Q_INVOKABLE void press(int row, const QString& rowKey, qreal rowTop, QPointF point, qreal viewportY);
    Q_INVOKABLE void move(QPointF point, qreal viewportY);
    // Ends the gesture: reports a tap or a drop (only when the row moved).
    Q_INVOKABLE void release();
    Q_INVOKABLE void cancel();
    // One auto-scroll frame: the list's next contentY, kept within
    // [minimum, maximum]; the lifted row and its slot follow the scroll.
    Q_INVOKABLE qreal scrolledContentY(qreal contentY, qreal minimum, qreal maximum);

    // The slot a row lifted from `from` lands in with the pointer at `y`:
    // before the first other row whose middle is below the pointer, else the
    // last slot. Rows start at 0 and are `rowHeight` apart.
    [[nodiscard]] static int dropSlot(int from, qreal y, qreal rowHeight, int rowCount);

signals:
    void layoutChanged();
    void dragChanged();
    // `rowKey` is the key the row had when pressed.
    void tapped(int row, const QString& rowKey);
    void dropped(int from, int to, const QString& rowKey);

private:
    enum class Mode { Idle, Pressing, Dragging };

    void startDrag();
    void updateTarget();
    void reset();

    QTimer m_hold;
    // When the last tap was reported: the second press of a double click is
    // not a second tap (the first may already have changed the rows).
    QElapsedTimer m_lastTap;
    Mode m_mode = Mode::Idle;
    qreal m_rowHeight = 0;
    int m_rowCount = 0;
    qreal m_viewportHeight = 0;
    int m_row = -1;
    QString m_key;
    int m_target = -1;
    // Where rows start in the content: the pressed row's top less its rows.
    qreal m_origin = 0;
    qreal m_startX = 0;
    qreal m_startY = 0;
    qreal m_pointerY = 0;
    qreal m_viewportY = 0;
    // The lifted row travelled far enough to count as a drag, not a hold.
    bool m_moved = false;
};

} // namespace lmp::ui
