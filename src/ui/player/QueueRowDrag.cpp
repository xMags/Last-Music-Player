#include "ui/player/QueueRowDrag.h"

#include <QGuiApplication>
#include <QStyleHints>

#include <algorithm>
#include <cmath>

namespace lmp::ui {

QueueRowDrag::QueueRowDrag(QObject* parent)
    : QObject(parent)
    , m_hold(this)
{
    m_hold.setSingleShot(true);
    m_hold.setInterval(HoldDelay);
    connect(&m_hold, &QTimer::timeout, this, &QueueRowDrag::startDrag);
}

void QueueRowDrag::setRowHeight(qreal height)
{
    if (height == m_rowHeight) {
        return;
    }
    m_rowHeight = height;
    emit layoutChanged();
}

void QueueRowDrag::setRowCount(int count)
{
    if (count == m_rowCount) {
        return;
    }
    m_rowCount = count;
    emit layoutChanged();
    // The list changed under the gesture (a song ended, a row left): a drop
    // would land on the wrong rows, so it is called off.
    if (m_mode != Mode::Idle) {
        cancel();
    }
}

void QueueRowDrag::setViewportHeight(qreal height)
{
    if (height == m_viewportHeight) {
        return;
    }
    m_viewportHeight = height;
    emit layoutChanged();
}

int QueueRowDrag::scrollStep() const
{
    if (!dragging()) {
        return 0;
    }
    if (m_viewportY < AutoScrollEdge) {
        return -AutoScrollStep;
    }
    if (m_viewportY > m_viewportHeight - AutoScrollEdge) {
        return AutoScrollStep;
    }
    return 0;
}

void QueueRowDrag::press(int row, const QString& rowKey, qreal rowTop, QPointF point, qreal viewportY)
{
    if (m_mode != Mode::Idle || row < 0 || row >= m_rowCount) {
        return;
    }
    m_mode = Mode::Pressing;
    m_row = row;
    m_key = rowKey;
    m_target = row;
    m_origin = rowTop - row * m_rowHeight;
    m_startX = point.x();
    m_startY = point.y();
    m_pointerY = point.y();
    m_viewportY = viewportY;
    m_moved = false;
    m_hold.start();
}

void QueueRowDrag::move(QPointF point, qreal viewportY)
{
    if (m_mode == Mode::Idle) {
        return;
    }
    const qreal dx = point.x() - m_startX;
    const qreal dy = point.y() - m_startY;
    m_pointerY = point.y();
    m_viewportY = viewportY;
    if (m_mode == Mode::Pressing) {
        if (std::hypot(dx, dy) < SlopPixels) {
            return;
        }
        if (std::abs(dy) < std::abs(dx)) {
            reset();
            return;
        }
        m_hold.stop();
        m_mode = Mode::Dragging;
    }
    m_moved = m_moved || std::abs(dy) >= SlopPixels / 2;
    updateTarget();
}

void QueueRowDrag::release()
{
    const Mode ended = m_mode;
    const int from = m_row;
    const int to = m_target;
    const QString key = m_key;
    const bool moved = m_moved;
    reset();
    if (ended == Mode::Pressing) {
        const int interval = QGuiApplication::styleHints()->mouseDoubleClickInterval();
        if (m_lastTap.isValid() && m_lastTap.elapsed() < interval) {
            return;
        }
        m_lastTap.start();
        emit tapped(from, key);
        return;
    }
    // A row held still and let go (a mouse that never moved) does nothing.
    if (ended == Mode::Dragging && moved && from != to) {
        emit dropped(from, to, key);
    }
}

void QueueRowDrag::cancel()
{
    reset();
}

qreal QueueRowDrag::scrolledContentY(qreal contentY, qreal minimum, qreal maximum)
{
    const int step = scrollStep();
    if (step == 0) {
        return contentY;
    }
    const qreal next = std::clamp(contentY + step, minimum, std::max(minimum, maximum));
    // The pointer stays put on screen, so in the content it moves with the scroll.
    m_pointerY += next - contentY;
    m_moved = m_moved || std::abs(m_pointerY - m_startY) >= SlopPixels / 2;
    updateTarget();
    return next;
}

int QueueRowDrag::dropSlot(int from, qreal y, qreal rowHeight, int rowCount)
{
    int slot = rowCount - 1;
    for (int index = 0; index < rowCount; ++index) {
        // The lifted row follows the pointer, so it never decides the slot.
        if (index == from) {
            continue;
        }
        if (y < index * rowHeight + rowHeight / 2) {
            slot = index > from ? index - 1 : index;
            break;
        }
    }
    return std::max(0, slot);
}

void QueueRowDrag::startDrag()
{
    m_hold.stop();
    if (m_mode != Mode::Pressing) {
        return;
    }
    m_mode = Mode::Dragging;
    updateTarget();
}

void QueueRowDrag::updateTarget()
{
    m_target = dropSlot(m_row, m_pointerY - m_origin, m_rowHeight, m_rowCount);
    emit dragChanged();
}

void QueueRowDrag::reset()
{
    m_hold.stop();
    const bool wasDragging = dragging();
    m_mode = Mode::Idle;
    m_row = -1;
    m_key.clear();
    m_target = -1;
    m_moved = false;
    if (wasDragging) {
        emit dragChanged();
    }
}

} // namespace lmp::ui
