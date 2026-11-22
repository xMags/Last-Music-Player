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
    // The CSS `ease` keyword.
    Q_PROPERTY(QEasingCurve ease READ ease CONSTANT FINAL)
    // The highlight of a segmented control sliding to a new choice.
    Q_PROPERTY(QEasingCurve segment READ segment CONSTANT FINAL)
    // The value bubble popping in over a slider.
    Q_PROPERTY(QEasingCurve bubblePop READ bubblePop CONSTANT FINAL)
    // The CSS keywords `ease-in` and `ease-out`, and a straight line.
    Q_PROPERTY(QEasingCurve easeIn READ easeIn CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve easeOut READ easeOut CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve easeInOut READ easeInOut CONSTANT FINAL)
    Q_PROPERTY(QEasingCurve linear READ linear CONSTANT FINAL)

    // Durations in milliseconds.
    Q_PROPERTY(int page READ page CONSTANT FINAL)
    Q_PROPERTY(int menuDuration READ menuDuration CONSTANT FINAL)
    Q_PROPERTY(int modal READ modal CONSTANT FINAL)
    Q_PROPERTY(int ripple READ ripple CONSTANT FINAL)
    Q_PROPERTY(int segmentGlide READ segmentGlide CONSTANT FINAL)
    Q_PROPERTY(int heart READ heart CONSTANT FINAL)
    Q_PROPERTY(int artCrossfade READ artCrossfade CONSTANT FINAL)
    Q_PROPERTY(int ambient READ ambient CONSTANT FINAL)
    // The song-colour glow: one sweep across the panel top, and its fade in or out.
    Q_PROPERTY(int ambientDrift READ ambientDrift CONSTANT FINAL)
    Q_PROPERTY(int ambientFade READ ambientFade CONSTANT FINAL)
    Q_PROPERTY(int rippleRing READ rippleRing CONSTANT FINAL)
    Q_PROPERTY(int rippleRingDelay READ rippleRingDelay CONSTANT FINAL)
    // Hover and press colour fades of buttons, chips and the play button.
    Q_PROPERTY(int controlFade READ controlFade CONSTANT FINAL)
    Q_PROPERTY(int chipFade READ chipFade CONSTANT FINAL)
    Q_PROPERTY(int playPress READ playPress CONSTANT FINAL)
    Q_PROPERTY(int sliderGlide READ sliderGlide CONSTANT FINAL)
    Q_PROPERTY(int sliderThumb READ sliderThumb CONSTANT FINAL)
    Q_PROPERTY(int bubbleFade READ bubbleFade CONSTANT FINAL)
    Q_PROPERTY(int bubbleScale READ bubbleScale CONSTANT FINAL)
    Q_PROPERTY(int switchToggle READ switchToggle CONSTANT FINAL)
    Q_PROPERTY(int popupOpen READ popupOpen CONSTANT FINAL)
    Q_PROPERTY(int chevronTurn READ chevronTurn CONSTANT FINAL)
    // The pause after typing before a search query is committed.
    Q_PROPERTY(int searchCommitDelay READ searchCommitDelay CONSTANT FINAL)
    Q_PROPERTY(int menuItemFade READ menuItemFade CONSTANT FINAL)
    Q_PROPERTY(int toastEnter READ toastEnter CONSTANT FINAL)
    Q_PROPERTY(int toastExit READ toastExit CONSTANT FINAL)
    Q_PROPERTY(int toastReflow READ toastReflow CONSTANT FINAL)
    // One full pass of the loading-skeleton sweep, pauses included.
    Q_PROPERTY(int skeletonSweep READ skeletonSweep CONSTANT FINAL)
    // How long a cover crossfade waits for the new picture.
    Q_PROPERTY(int artRevealTimeout READ artRevealTimeout CONSTANT FINAL)
    Q_PROPERTY(int navItemFade READ navItemFade CONSTANT FINAL)
    // The logo's bars while music plays: one ripple, and the delay per bar
    // outward from the middle.
    Q_PROPERTY(int logoRipple READ logoRipple CONSTANT FINAL)
    Q_PROPERTY(int logoRippleStep READ logoRippleStep CONSTANT FINAL)
    // A new song in the player bar: the old title and artist leave (ease-in),
    // the new ones rise in after a short delay and the cover settles (water).
    Q_PROPERTY(int songChangeOut READ songChangeOut CONSTANT FINAL)
    Q_PROPERTY(int songChangeIn READ songChangeIn CONSTANT FINAL)
    Q_PROPERTY(int songChangeInDelay READ songChangeInDelay CONSTANT FINAL)
    Q_PROPERTY(int songChangeCover READ songChangeCover CONSTANT FINAL)
    // How far, in pixels, the text travels.
    Q_PROPERTY(int songChangeShift READ songChangeShift CONSTANT FINAL)

    Q_PROPERTY(bool reduced READ reduced WRITE setReduced NOTIFY reducedChanged FINAL)

public:
    explicit Motion(QObject* parent = nullptr);

    [[nodiscard]] QEasingCurve standard() const { return m_standard; }
    [[nodiscard]] QEasingCurve water() const { return m_water; }
    [[nodiscard]] QEasingCurve toastSpring() const { return m_toastSpring; }
    [[nodiscard]] QEasingCurve menu() const { return m_menu; }
    [[nodiscard]] QEasingCurve ease() const { return m_ease; }
    [[nodiscard]] QEasingCurve segment() const { return m_segment; }
    [[nodiscard]] QEasingCurve bubblePop() const { return m_bubblePop; }
    [[nodiscard]] QEasingCurve easeIn() const { return m_easeIn; }
    [[nodiscard]] QEasingCurve easeOut() const { return m_easeOut; }
    [[nodiscard]] QEasingCurve easeInOut() const { return m_easeInOut; }
    [[nodiscard]] QEasingCurve linear() const { return QEasingCurve(QEasingCurve::Linear); }

    [[nodiscard]] int page() const { return 280; }
    [[nodiscard]] int menuDuration() const { return 170; }
    [[nodiscard]] int modal() const { return 160; }
    [[nodiscard]] int ripple() const { return 620; }
    [[nodiscard]] int segmentGlide() const { return 260; }
    [[nodiscard]] int heart() const { return 640; }
    [[nodiscard]] int artCrossfade() const { return 320; }
    [[nodiscard]] int ambient() const { return 1200; }
    [[nodiscard]] int ambientDrift() const { return 26000; }
    [[nodiscard]] int ambientFade() const { return 1400; }
    [[nodiscard]] int rippleRing() const { return 560; }
    [[nodiscard]] int rippleRingDelay() const { return 60; }
    [[nodiscard]] int controlFade() const { return 120; }
    [[nodiscard]] int chipFade() const { return 150; }
    [[nodiscard]] int playPress() const { return 100; }
    [[nodiscard]] int sliderGlide() const { return 140; }
    [[nodiscard]] int sliderThumb() const { return 100; }
    [[nodiscard]] int bubbleFade() const { return 120; }
    [[nodiscard]] int bubbleScale() const { return 160; }
    [[nodiscard]] int switchToggle() const { return 160; }
    [[nodiscard]] int popupOpen() const { return 120; }
    [[nodiscard]] int chevronTurn() const { return 150; }
    [[nodiscard]] int searchCommitDelay() const { return 350; }
    [[nodiscard]] int menuItemFade() const { return 80; }
    [[nodiscard]] int toastEnter() const { return 420; }
    [[nodiscard]] int toastExit() const { return 200; }
    [[nodiscard]] int toastReflow() const { return 280; }
    [[nodiscard]] int skeletonSweep() const { return 2800; }
    [[nodiscard]] int artRevealTimeout() const { return 2500; }
    [[nodiscard]] int navItemFade() const { return 100; }
    [[nodiscard]] int logoRipple() const { return 1900; }
    [[nodiscard]] int logoRippleStep() const { return 160; }
    [[nodiscard]] int songChangeOut() const { return 220; }
    [[nodiscard]] int songChangeIn() const { return 320; }
    [[nodiscard]] int songChangeInDelay() const { return 90; }
    [[nodiscard]] int songChangeCover() const { return 380; }
    [[nodiscard]] int songChangeShift() const { return 10; }

    [[nodiscard]] bool reduced() const { return m_reduced; }
    void setReduced(bool reduced);

    // A CSS cubic-bezier(x1, y1, x2, y2) timing function.
    [[nodiscard]] static QEasingCurve cubicBezier(qreal x1, qreal y1, qreal x2, qreal y2);

    // Diameter of a ripple pressed at (x, y) in a width x height box: twice
    // the distance to the farthest corner, so the disc reaches every corner.
    Q_INVOKABLE qreal rippleDiameter(qreal width, qreal height, qreal x, qreal y) const;

    // A new toast springs up: keyframes at 0, 70% and 100% (opacity 0, 1, 1;
    // rise 26px, -3px, 0; scale 0.92, 1.01, 1) joined by straight lines, with
    // the spring curve applied to the whole run. `progress` is the curve's
    // output, so it overshoots 1 and the last segment is extended.
    Q_INVOKABLE qreal toastEnterOpacity(qreal progress) const;
    Q_INVOKABLE qreal toastEnterOffset(qreal progress) const;
    Q_INVOKABLE qreal toastEnterScale(qreal progress) const;
    // The cover of a new song: scale 0.9, 1.04 at 60% and 1 at the end,
    // straight lines between, the curve applied to the whole run.
    Q_INVOKABLE qreal songChangeCoverScale(qreal progress) const;

signals:
    void reducedChanged();

private:
    QEasingCurve m_standard = cubicBezier(0.2, 0, 0, 1);
    QEasingCurve m_water = cubicBezier(0.22, 0.61, 0.36, 1);
    QEasingCurve m_toastSpring = cubicBezier(0.34, 1.45, 0.5, 1);
    QEasingCurve m_menu = cubicBezier(0.1, 0.9, 0.2, 1);
    QEasingCurve m_ease = cubicBezier(0.25, 0.1, 0.25, 1);
    QEasingCurve m_segment = cubicBezier(0.3, 0.7, 0.2, 1);
    QEasingCurve m_bubblePop = cubicBezier(0.3, 1.4, 0.5, 1);
    QEasingCurve m_easeIn = cubicBezier(0.42, 0, 1, 1);
    QEasingCurve m_easeOut = cubicBezier(0, 0, 0.58, 1);
    QEasingCurve m_easeInOut = cubicBezier(0.42, 0, 0.58, 1);
    bool m_reduced = false;
};

} // namespace lmp::ui
