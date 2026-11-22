#include "ui/theme/Motion.h"

#include <QPointF>

#include <algorithm>
#include <cmath>

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

qreal Motion::rippleDiameter(qreal width, qreal height, qreal x, qreal y) const
{
    const qreal dx = std::max(x, width - x);
    const qreal dy = std::max(y, height - y);
    return std::hypot(dx, dy) * 2;
}

namespace {

// Straight-line keyframes at 0, `at` and 1, extended past both ends.
qreal threeKeyframes(qreal progress, qreal start, qreal middle, qreal end, qreal at = 0.7)
{
    if (progress < at) {
        return start + (middle - start) * (progress / at);
    }
    return middle + (end - middle) * ((progress - at) / (1 - at));
}

} // namespace

qreal Motion::toastEnterOpacity(qreal progress) const
{
    return std::clamp(threeKeyframes(progress, 0, 1, 1), 0.0, 1.0);
}

qreal Motion::toastEnterOffset(qreal progress) const
{
    return threeKeyframes(progress, 26, -3, 0);
}

qreal Motion::toastEnterScale(qreal progress) const
{
    return threeKeyframes(progress, 0.92, 1.01, 1);
}

qreal Motion::songChangeCoverScale(qreal progress) const
{
    return threeKeyframes(progress, 0.9, 1.04, 1, 0.6);
}

QEasingCurve Motion::cubicBezier(qreal x1, qreal y1, qreal x2, qreal y2)
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1, 1));
    return curve;
}

} // namespace lmp::ui
