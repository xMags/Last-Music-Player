#include "ui/theme/Motion.h"

#include <QPointF>

namespace lmp::ui {

Motion::Motion(QObject* parent)
    : QObject(parent)
{
}

void Motion::setReduced(bool reduced)
{
    if (reduced == m_reduced) {
        return;
    }
    m_reduced = reduced;
    emit reducedChanged();
}

QEasingCurve Motion::cubicBezier(qreal x1, qreal y1, qreal x2, qreal y2)
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1, 1));
    return curve;
}

} // namespace lmp::ui
