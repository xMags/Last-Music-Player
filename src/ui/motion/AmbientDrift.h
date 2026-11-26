#pragma once

#include "ui/motion/AmbientPhase.h"

#include <QEasingCurve>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// One place of the drifting glow: a share of its width sideways, pixels
// down, and its scale.
struct DriftPose {
    qreal x = 0;
    qreal y = 0;
    qreal scale = 1;

    friend bool operator==(const DriftPose&, const DriftPose&) = default;
};

// Where the content panel's drifting glow is (ambient-drift, motion.css
// 249-290), stepped by an AmbientPhase whose period is one loop there and
// back. Keyframes, as (share of the glow's width, pixels down, scale):
// (-0.14, -270, 1), (0.11, -210, 1.12), (0.32, -260, 0.94), (0.05, -190,
// 1.06), then back the same way, the legs taking 0.35, 0.35, 0.30, 0.30,
// 0.35 and 0.35 of half the loop, each eased by `easing`.
//
// Each time the clock starts, the first leg begins from wherever the glow
// was left when it stopped, not from the first keyframe, so resuming never
// makes it jump; every later loop starts at the first keyframe.
//
// GUI thread only.
class AmbientDrift : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(lmp::ui::AmbientPhase* clock READ clock WRITE setClock NOTIFY clockChanged FINAL)
    Q_PROPERTY(QEasingCurve easing READ easing WRITE setEasing NOTIFY easingChanged FINAL)
    Q_PROPERTY(qreal x READ x NOTIFY poseChanged FINAL)
    Q_PROPERTY(qreal y READ y NOTIFY poseChanged FINAL)
    Q_PROPERTY(qreal scale READ scale NOTIFY poseChanged FINAL)

public:
    using Pose = DriftPose;

    explicit AmbientDrift(QObject* parent = nullptr);

    [[nodiscard]] AmbientPhase* clock() const { return m_clock; }
    void setClock(AmbientPhase* clock);
    [[nodiscard]] QEasingCurve easing() const { return m_easing; }
    void setEasing(const QEasingCurve& easing);
    [[nodiscard]] qreal x() const { return m_pose.x; }
    [[nodiscard]] qreal y() const { return m_pose.y; }
    [[nodiscard]] qreal scale() const { return m_pose.scale; }

    // The first keyframe, where the glow rests before it ever moves.
    [[nodiscard]] static Pose start();
    // The pose at `phase` (0 to 1 through the loop there and back) when the
    // first leg begins at `from`.
    [[nodiscard]] static Pose poseAt(qreal phase, const Pose& from, const QEasingCurve& easing);

signals:
    void clockChanged();
    void easingChanged();
    void poseChanged();

private:
    void clockStateChanged();
    void recompute();

    QPointer<AmbientPhase> m_clock;
    QEasingCurve m_easing;
    Pose m_pose = start();
    // Where the first leg of the current run begins.
    Pose m_from = start();
};

} // namespace lmp::ui
