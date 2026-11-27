#include "ui/motion/AmbientPhase.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace lmp::ui {

namespace {

constexpr qreal NsPerMs = 1e6;

} // namespace

AmbientPhase::AmbientPhase(QObject* parent)
    : QObject(parent)
{
    m_steady.start();
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        step();
        scheduleStep();
    });
}

void AmbientPhase::setRunning(bool running)
{
    if (running == m_running) {
        return;
    }
    m_running = running;
    emit runningChanged();
    updateActive();
}

void AmbientPhase::setPeriod(int period)
{
    if (period == m_period) {
        return;
    }
    m_period = period;
    emit periodChanged();
    if (m_active) {
        emit phaseChanged();
    }
    updateActive();
}

void AmbientPhase::setResume(bool resume)
{
    if (resume == m_resume) {
        return;
    }
    m_resume = resume;
    emit resumeChanged();
}

void AmbientPhase::setStepInterval(int milliseconds)
{
    const int interval = std::max(1, milliseconds);
    if (interval == m_stepInterval) {
        return;
    }
    // Steps read real time, so the motion keeps its pace.
    m_stepInterval = interval;
    if (m_timer.isActive()) {
        scheduleStep();
    }
    emit stepIntervalChanged();
}

void AmbientPhase::setHeld(bool held)
{
    if (held == m_held) {
        return;
    }
    m_held = held;
    emit heldChanged();
    if (!m_active) {
        return;
    }
    if (m_held) {
        m_timer.stop();
        return;
    }
    // The held time does not count: the motion goes on from where it stopped.
    m_startNs = nowNs() - static_cast<qint64>(m_elapsedMs * NsPerMs);
    scheduleStep();
}

qreal AmbientPhase::phase() const
{
    if (m_period <= 0) {
        return 0;
    }
    return std::fmod(m_elapsedMs, static_cast<qreal>(m_period)) / m_period;
}

void AmbientPhase::setClock(std::function<qint64()> nowNs)
{
    m_clock = std::move(nowNs);
}

void AmbientPhase::step()
{
    if (!m_active || m_held) {
        return;
    }
    const qreal elapsedMs = static_cast<qreal>(nowNs() - m_startNs) / NsPerMs;
    // A clock never runs backwards, but a replaced one might.
    if (elapsedMs <= m_elapsedMs) {
        return;
    }
    m_elapsedMs = elapsedMs;
    emit phaseChanged();
}

qint64 AmbientPhase::nowNs() const
{
    return m_clock ? m_clock() : m_steady.nsecsElapsed();
}

// Every motion steps on the same grid of the steady clock (whole multiples
// of its interval since the clock's start), not at its own start time plus
// whole intervals: motions with the same interval, or intervals that divide
// each other, then change in the same frame instead of each asking for a
// frame of its own between the others'.
void AmbientPhase::scheduleStep()
{
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    const qint64 next = (nowMs / m_stepInterval + 1) * m_stepInterval;
    m_timer.start(static_cast<int>(std::max<qint64>(1, next - nowMs)));
}

void AmbientPhase::updateActive()
{
    const bool active = m_running && m_period > 0;
    if (active == m_active) {
        return;
    }
    if (!active) {
        m_timer.stop();
        m_active = false;
        emit activeChanged();
        return;
    }
    if (!m_resume && m_elapsedMs != 0) {
        m_elapsedMs = 0;
        emit phaseChanged();
    }
    m_startNs = nowNs() - static_cast<qint64>(m_elapsedMs * NsPerMs);
    m_active = true;
    if (!m_held) {
        scheduleStep();
    }
    emit activeChanged();
}

} // namespace lmp::ui
