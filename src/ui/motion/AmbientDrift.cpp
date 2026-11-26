#include "ui/motion/AmbientDrift.h"

#include <algorithm>
#include <array>
#include <cstddef>

namespace lmp::ui {

namespace {

// The keyframes in order, there and back; the last equals the first.
constexpr std::array<DriftPose, 7> Keyframes = {{
    {-0.14, -270, 1},
    {0.11, -210, 1.12},
    {0.32, -260, 0.94},
    {0.05, -190, 1.06},
    {0.32, -260, 0.94},
    {0.11, -210, 1.12},
    {-0.14, -270, 1},
}};

// Each leg's share of the loop: 0.35, 0.35, 0.30 of one way, then mirrored.
constexpr std::array<qreal, 6> LegShares = {0.175, 0.175, 0.15, 0.15, 0.175, 0.175};

[[nodiscard]] qreal lerp(qreal a, qreal b, qreal t)
{
    return a + (b - a) * t;
}

} // namespace

AmbientDrift::AmbientDrift(QObject* parent)
    : QObject(parent)
{
}

void AmbientDrift::setClock(AmbientPhase* clock)
{
    if (clock == m_clock) {
        return;
    }
    if (m_clock) {
        disconnect(m_clock, nullptr, this, nullptr);
    }
    m_clock = clock;
    if (m_clock) {
        connect(m_clock, &AmbientPhase::phaseChanged, this, &AmbientDrift::recompute);
        connect(m_clock, &AmbientPhase::activeChanged, this, &AmbientDrift::clockStateChanged);
    }
    emit clockChanged();
    clockStateChanged();
}

void AmbientDrift::setEasing(const QEasingCurve& easing)
{
    if (easing == m_easing) {
        return;
    }
    m_easing = easing;
    emit easingChanged();
    recompute();
}

DriftPose AmbientDrift::start()
{
    return Keyframes.front();
}

DriftPose AmbientDrift::poseAt(qreal phase, const Pose& from, const QEasingCurve& easing)
{
    // Find the leg phase falls in; the last leg also takes anything past 1.
    std::size_t leg = 0;
    qreal legStart = 0;
    while (leg + 1 < LegShares.size() && phase >= legStart + LegShares[leg]) {
        legStart += LegShares[leg];
        ++leg;
    }
    const Pose& a = leg == 0 ? from : Keyframes[leg];
    const Pose& b = Keyframes[leg + 1];
    const qreal t = easing.valueForProgress(std::clamp((phase - legStart) / LegShares[leg], 0.0, 1.0));
    return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.scale, b.scale, t)};
}

void AmbientDrift::clockStateChanged()
{
    // A new run: its first leg sets off from where the glow stands now.
    if (m_clock && m_clock->active()) {
        m_from = m_pose;
    }
    recompute();
}

void AmbientDrift::recompute()
{
    if (!m_clock || !m_clock->active() || m_clock->period() <= 0) {
        return;
    }
    const bool firstLoop = m_clock->elapsed() < m_clock->period();
    const Pose pose = poseAt(m_clock->phase(), firstLoop ? m_from : start(), m_easing);
    if (pose == m_pose) {
        return;
    }
    m_pose = pose;
    emit poseChanged();
}

} // namespace lmp::ui
