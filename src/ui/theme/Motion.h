#pragma once

#include <QEasingCurve>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// Easing curves and durations of every animation, and whether the user asked
// the system to reduce motion. Animations check `reduced` when they start and
// collapse to an instant change while it is true.
class Motion : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QEasingCurve standard READ standard CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve water READ water CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve toastSpring READ toastSpring CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve menu READ menu CONSTANT FINAL)

    // Durations in milliseconds.
    Q_PROPERTY(int page READ page CONSTANT FINAL)
    Q_PROPERTY(int menuDuration READ menuDuration CONSTANT FINAL)
    Q_PROPERTY(int modal READ modal CONSTANT FINAL)
    Q_PROPERTY(int ripple READ ripple CONSTANT FINAL)
    Q_PROPERTY(int segmentGlide READ segmentGlide CONSTANT FINAL)
    Q_PROPERTY(int heart READ heart CONSTANT FINAL)
    Q_PROPERTY(int artCrossfade READ artCrossfade CONSTANT FINAL)
    Q_PROPERTY(int ambient READ ambient CONSTANT FINAL)

    Q_PROPERTY(bool reduced READ reduced WRITE setReduced NOTIFY reducedChanged FINAL)

public:
    explicit Motion(QObject* parent = nullptr);

    [[nodiscard]] QEasingCurve standard() const { return m_standard; }
    [[nodiscard]] QEasingCurve water() const { return m_water; }
    [[nodiscard]] QEasingCurve toastSpring() const { return m_toastSpring; }
    [[nodiscard]] QEasingCurve menu() const { return m_menu; }

    [[nodiscard]] int page() const { return 280; }
    [[nodiscard]] int menuDuration() const { return 170; }
    [[nodiscard]] int modal() const { return 160; }
    [[nodiscard]] int ripple() const { return 620; }
    [[nodiscard]] int segmentGlide() const { return 260; }
    [[nodiscard]] int heart() const { return 640; }
    [[nodiscard]] int artCrossfade() const { return 320; }
    [[nodiscard]] int ambient() const { return 1200; }

    [[nodiscard]] bool reduced() const { return m_reduced; }
    void setReduced(bool reduced);

    // A CSS cubic-bezier(x1, y1, x2, y2) timing function.
    [[nodiscard]] static QEasingCurve cubicBezier(qreal x1, qreal y1, qreal x2, qreal y2);

signals:
    void reducedChanged();

private:
    QEasingCurve m_standard = cubicBezier(0.2, 0, 0, 1);
    QEasingCurve m_water = cubicBezier(0.22, 0.61, 0.36, 1);
    QEasingCurve m_toastSpring = cubicBezier(0.34, 1.45, 0.5, 1);
    QEasingCurve m_menu = cubicBezier(0.1, 0.9, 0.2, 1);
    bool m_reduced = false;
};

} // namespace lmp::ui
