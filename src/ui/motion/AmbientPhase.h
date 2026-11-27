#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace lmp::ui {

// The clock of a slow, endless ambient motion (a breathing glow, a drifting
// light, bouncing bars). It steps about 60 times a second from a plain timer
// instead of a Qt animation: a running animation of any kind makes the scene
// graph render the whole window on every vertical refresh, and these motions
// move a fraction of a pixel per frame. Each step reads real elapsed time, so
// a late tick never slows the motion down.
//
// The motion is under way (`active`) while `running` is true and `period` is
// positive. Otherwise no timer runs and the values hold where they are. When
// it starts again it begins at 0, unless `resume` is set, in which case it
// continues where it stopped. The reset to 0 is announced (phaseChanged)
// while `active` is still false, before activeChanged, so a listener reacting
// to the start already sees the new time.
//
// `held` freezes a motion that is under way without stopping it: the timer
// pauses, the values hold and `active` stays true, and on release the motion
// goes on from where it was held (the held time does not count). It is for
// short pauses the motion should not visibly restart after, such as while
// the window is being dragged.
//
// GUI thread only.
class AmbientPhase : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged FINAL)
    // One loop in milliseconds.
    Q_PROPERTY(int period READ period WRITE setPeriod NOTIFY periodChanged FINAL)
    Q_PROPERTY(bool resume READ resume WRITE setResume NOTIFY resumeChanged FINAL)
    Q_PROPERTY(bool held READ held WRITE setHeld NOTIFY heldChanged FINAL)
    // Milliseconds between steps. Each step redraws the window, so a slow
    // motion that moves well under a pixel a step takes fewer of them.
    Q_PROPERTY(int stepInterval READ stepInterval WRITE setStepInterval NOTIFY stepIntervalChanged FINAL)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
    // 0 to 1 through the current loop, straight in time.
    Q_PROPERTY(qreal phase READ phase NOTIFY phaseChanged FINAL)
    // Milliseconds of motion since the start (summed over every run when
    // `resume` is set).
    Q_PROPERTY(qreal elapsed READ elapsed NOTIFY phaseChanged FINAL)

public:
    // Time between steps by default: about 60 a second.
    static constexpr int StepIntervalMs = 16;

    explicit AmbientPhase(QObject* parent = nullptr);

    [[nodiscard]] bool running() const { return m_running; }
    void setRunning(bool running);
    [[nodiscard]] int period() const { return m_period; }
    void setPeriod(int period);
    [[nodiscard]] bool resume() const { return m_resume; }
    void setResume(bool resume);
    [[nodiscard]] bool held() const { return m_held; }
    void setHeld(bool held);
    [[nodiscard]] int stepInterval() const { return m_stepInterval; }
    // Clamped to at least one millisecond.
    void setStepInterval(int milliseconds);
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] qreal phase() const;
    [[nodiscard]] qreal elapsed() const { return m_elapsedMs; }

    // Replaces the steady clock, in nanoseconds (tests pass their own).
    void setClock(std::function<qint64()> nowNs);
    // Whether the step timer is running.
    [[nodiscard]] bool stepping() const { return m_timer.isActive(); }
    // Milliseconds until the next step, or -1 without one.
    [[nodiscard]] int nextStepInMs() const { return m_timer.remainingTime(); }
    // Reads the clock and moves the phase on; the timer calls it.
    void step();

signals:
    void runningChanged();
    void periodChanged();
    void resumeChanged();
    void heldChanged();
    void stepIntervalChanged();
    void activeChanged();
    void phaseChanged();

private:
    [[nodiscard]] qint64 nowNs() const;
    // Starts or stops the motion after `running` or `period` changed.
    void updateActive();
    // Starts the timer for the next step.
    void scheduleStep();

    QTimer m_timer;
    QElapsedTimer m_steady;
    std::function<qint64()> m_clock;
    qint64 m_startNs = 0;
    qreal m_elapsedMs = 0;
    int m_period = 0;
    int m_stepInterval = StepIntervalMs;
    bool m_running = false;
    bool m_resume = false;
    bool m_held = false;
    bool m_active = false;
};

} // namespace lmp::ui
