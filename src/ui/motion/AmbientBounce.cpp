#include "ui/motion/AmbientBounce.h"

#include <algorithm>
#include <cmath>

namespace lmp::ui {

AmbientBounce::AmbientBounce(QObject* parent)
    : QObject(parent)
{
}

void AmbientBounce::setClock(AmbientPhase* clock)
{
    if (clock == m_clock) {
        return;
    }
    if (m_clock) {
        disconnect(m_clock, nullptr, this, nullptr);
    }
    m_clock = clock;
    if (m_clock) {
        connect(m_clock, &AmbientPhase::phaseChanged, this, &AmbientBounce::recompute);
        connect(m_clock, &AmbientPhase::activeChanged, this, &AmbientBounce::recompute);
    }
    emit clockChanged();
    recompute();
}

void AmbientBounce::setDelay(int delay)
{
    if (delay == m_delay) {
        return;
    }
    m_delay = delay;
    emit settingsChanged();
    recompute();
}

void AmbientBounce::setHalfPeriod(int halfPeriod)
{
    if (halfPeriod == m_halfPeriod) {
        return;
    }
    m_halfPeriod = halfPeriod;
    emit settingsChanged();
    recompute();
}

void AmbientBounce::setFrom(qreal from)
{
    if (from == m_from) {
        return;
    }
    m_from = from;
    emit settingsChanged();
    recompute();
}

void AmbientBounce::setTo(qreal to)
{
    if (to == m_to) {
        return;
    }
    m_to = to;
    emit settingsChanged();
    recompute();
}

void AmbientBounce::setEasing(const QEasingCurve& easing)
{
    if (easing == m_easing) {
        return;
    }
    m_easing = easing;
    emit settingsChanged();
    recompute();
}

qreal AmbientBounce::valueAt(qreal sinceDelayMs, int halfPeriod, qreal from, qreal to, const QEasingCurve& easing)
{
    if (halfPeriod <= 0) {
        return from;
    }
    const qreal half = halfPeriod;
    const qreal within = std::fmod(std::max(sinceDelayMs, 0.0), 2 * half);
    if (within < half) {
        return from + (to - from) * easing.valueForProgress(within / half);
    }
    return to + (from - to) * easing.valueForProgress((within - half) / half);
}

void AmbientBounce::recompute()
{
    const bool holding = !m_clock || !m_clock->active() || m_halfPeriod <= 0 || m_clock->elapsed() < m_delay;
    const bool wasHolding = m_holding;
    if (holding != m_holding) {
        m_holding = holding;
        emit holdingChanged();
    }
    if (holding) {
        return;
    }
    const qreal value = valueAt(m_clock->elapsed() - m_delay, m_halfPeriod, m_from, m_to, m_easing);
    if (value == m_value && !wasHolding) {
        return;
    }
    m_value = value;
    emit valueChanged();
}

} // namespace lmp::ui
