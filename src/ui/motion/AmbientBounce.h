#pragma once

#include "ui/motion/AmbientPhase.h"

#include <QEasingCurve>
#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// A value bouncing without end, stepped by an AmbientPhase: after `delay`
// milliseconds from the start it goes from `from` to `to` over `halfPeriod`
// and back over another `halfPeriod`, eased by `easing` each way, again and
// again. Several bounces may share one clock and differ only in their
// delay, as bars rippling one after another do.
//
// `holding` is true while the clock is stopped and during the delay; the
// owner then shows its own resting value. `value` changes only while it is
// false, and valueChanged is also emitted when the delay ends, so an owner
// that writes the value somewhere receives the first step even when the
// number happens to be unchanged.
//
// GUI thread only.
class AmbientBounce : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(lmp::ui::AmbientPhase* clock READ clock WRITE setClock NOTIFY clockChanged FINAL)
    Q_PROPERTY(int delay READ delay WRITE setDelay NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int halfPeriod READ halfPeriod WRITE setHalfPeriod NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal from READ from WRITE setFrom NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal to READ to WRITE setTo NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QEasingCurve easing READ easing WRITE setEasing NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal value READ value NOTIFY valueChanged FINAL)
    Q_PROPERTY(bool holding READ holding NOTIFY holdingChanged FINAL)

public:
    explicit AmbientBounce(QObject* parent = nullptr);

    [[nodiscard]] AmbientPhase* clock() const { return m_clock; }
    void setClock(AmbientPhase* clock);
    [[nodiscard]] int delay() const { return m_delay; }
    void setDelay(int delay);
    [[nodiscard]] int halfPeriod() const { return m_halfPeriod; }
    void setHalfPeriod(int halfPeriod);
    [[nodiscard]] qreal from() const { return m_from; }
    void setFrom(qreal from);
    [[nodiscard]] qreal to() const { return m_to; }
    void setTo(qreal to);
    [[nodiscard]] QEasingCurve easing() const { return m_easing; }
    void setEasing(const QEasingCurve& easing);
    [[nodiscard]] qreal value() const { return m_value; }
    [[nodiscard]] bool holding() const { return m_holding; }

    // The value `sinceDelayMs` milliseconds after the delay ended.
    [[nodiscard]] static qreal valueAt(qreal sinceDelayMs, int halfPeriod, qreal from, qreal to,
                                       const QEasingCurve& easing);

signals:
    void clockChanged();
    void settingsChanged();
    void valueChanged();
    void holdingChanged();

private:
    void recompute();

    QPointer<AmbientPhase> m_clock;
    QEasingCurve m_easing;
    qreal m_from = 0;
    qreal m_to = 1;
    qreal m_value = 0;
    int m_delay = 0;
    int m_halfPeriod = 0;
    bool m_holding = true;
};

} // namespace lmp::ui
