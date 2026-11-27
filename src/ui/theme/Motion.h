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
    // The cover flight between a card and a page header (flight.ts).
    Q_PROPERTY(QEasingCurve flight READ flight CONSTANT FINAL)
    Q_PROPERTY(int coverFlight READ coverFlight CONSTANT FINAL)
    // The queue arc (queue-fly.ts).
    Q_PROPERTY(QEasingCurve queueArc READ queueArc CONSTANT FINAL)
    // A new artist in the welcome flow (welcome-artist-in).
    Q_PROPERTY(QEasingCurve artistPop READ artistPop CONSTANT FINAL)
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
    // .settings-button and .settings-row-switch: background over .15s.
    Q_PROPERTY(int settingsFade READ settingsFade CONSTANT FINAL)
    // A Settings dialog rising in (settings-dialog-in, .18s).
    Q_PROPERTY(int settingsDialogIn READ settingsDialogIn CONSTANT FINAL)
    // A search top card's Play or Open growing on hover (.18s).
    Q_PROPERTY(int searchTopAction READ searchTopAction CONSTANT FINAL)
    // A card's cover zooms on hover (styles.css 593-602).
    Q_PROPERTY(int cardZoom READ cardZoom CONSTANT FINAL)
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
    // The Up Next list: rows glide to new places, new rows slide in from
    // listEnterShift to the right (each listEnterStagger after the one
    // before), leaving rows fade, shrink to listLeaveScale and drift left.
    Q_PROPERTY(int listGlide READ listGlide CONSTANT FINAL)
    Q_PROPERTY(int listEnterDelay READ listEnterDelay CONSTANT FINAL)
    Q_PROPERTY(int listEnterStagger READ listEnterStagger CONSTANT FINAL)
    Q_PROPERTY(int listLeave READ listLeave CONSTANT FINAL)
    Q_PROPERTY(int listEnterShift READ listEnterShift CONSTANT FINAL)
    Q_PROPERTY(int listLeaveShift READ listLeaveShift CONSTANT FINAL)
    Q_PROPERTY(qreal listLeaveScale READ listLeaveScale CONSTANT FINAL)
    // A queue row settling back after it was carried.
    Q_PROPERTY(int queueRowSettle READ queueRowSettle CONSTANT FINAL)
    // One breath of the glow under the playing song's cover, and how it
    // rests while it does not breathe (reduced motion).
    Q_PROPERTY(int nowPlayingBreathe READ nowPlayingBreathe CONSTANT FINAL)
    Q_PROPERTY(qreal glowRestScale READ glowRestScale CONSTANT FINAL)
    Q_PROPERTY(qreal glowRestOpacity READ glowRestOpacity CONSTANT FINAL)
    // Milliseconds between steps of the slowest endless motions (the hero
    // field's breath, the playing glow, the ambient drift): about 30 a
    // second, as they move well under a pixel a step (AmbientPhase).
    Q_PROPERTY(int ambientSlowStep READ ambientSlowStep CONSTANT FINAL)

    Q_PROPERTY(bool reduced READ reduced WRITE setReduced NOTIFY reducedChanged FINAL)
    // False while the main window is minimised or hidden (closed to the
    // tray): animations that loop without end stop, so nothing ticks for a
    // window no one can see.
    Q_PROPERTY(bool windowShown READ windowShown WRITE setWindowShown NOTIFY windowShownChanged FINAL)
    // True while the endless motions hold where they are (AmbientPhase.held):
    // while the main window is dragged to another place, so no frame is drawn
    // while the system moves it (a resize redraws on every step anyway, so
    // the motions go on then), and while another window is in front, so a
    // player left open behind other work draws nothing.
    Q_PROPERTY(bool ambientHeld READ ambientHeld NOTIFY ambientHeldChanged FINAL)

public:
    explicit Motion(QObject* parent = nullptr);

    [[nodiscard]] QEasingCurve standard() const { return m_standard; }
    [[nodiscard]] QEasingCurve flight() const { return m_flight; }
    [[nodiscard]] int coverFlight() const { return 420; }
    [[nodiscard]] QEasingCurve queueArc() const { return m_queueArc; }
    [[nodiscard]] QEasingCurve artistPop() const { return m_artistPop; }
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
    [[nodiscard]] int settingsFade() const { return 150; }
    [[nodiscard]] int settingsDialogIn() const { return 180; }
    [[nodiscard]] int searchTopAction() const { return 180; }
    [[nodiscard]] int cardZoom() const { return 450; }
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
    [[nodiscard]] int skeletonSweep() const { return 2200; }
    [[nodiscard]] int artRevealTimeout() const { return 2500; }
    [[nodiscard]] int navItemFade() const { return 100; }
    [[nodiscard]] int logoRipple() const { return 1900; }
    [[nodiscard]] int logoRippleStep() const { return 160; }
    [[nodiscard]] int songChangeOut() const { return 220; }
    [[nodiscard]] int songChangeIn() const { return 320; }
    [[nodiscard]] int songChangeInDelay() const { return 90; }
    [[nodiscard]] int songChangeCover() const { return 380; }
    [[nodiscard]] int songChangeShift() const { return 10; }
    [[nodiscard]] int listGlide() const { return 300; }
    [[nodiscard]] int listEnterDelay() const { return 60; }
    [[nodiscard]] int listEnterStagger() const { return 30; }
    [[nodiscard]] int listLeave() const { return 240; }
    [[nodiscard]] int listEnterShift() const { return 14; }
    [[nodiscard]] int listLeaveShift() const { return 10; }
    [[nodiscard]] qreal listLeaveScale() const { return 0.94; }
    [[nodiscard]] int queueRowSettle() const { return 160; }
    [[nodiscard]] int nowPlayingBreathe() const { return 6500; }
    [[nodiscard]] qreal glowRestScale() const { return 0.96; }
    [[nodiscard]] qreal glowRestOpacity() const { return 0.7; }
    [[nodiscard]] int ambientSlowStep() const { return 33; }

    [[nodiscard]] bool reduced() const { return m_reduced; }
    void setReduced(bool reduced);
    [[nodiscard]] bool windowShown() const { return m_windowShown; }
    void setWindowShown(bool shown);
    [[nodiscard]] bool ambientHeld() const { return m_windowMoving || !m_windowActive; }
    // The main window is being dragged to another place.
    void setWindowMoving(bool moving);
    // The main window (or one of its popups) has the focus.
    void setWindowActive(bool active);

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
    // The glow's breath at `phase` (0 to 1, straight in time): scale 0.94
    // and opacity 0.55 at both ends, 1.06 and 0.95 halfway, eased in and out
    // between.
    Q_INVOKABLE qreal glowBreathScale(qreal phase) const;
    Q_INVOKABLE qreal glowBreathOpacity(qreal phase) const;

signals:
    void reducedChanged();
    void windowShownChanged();
    void ambientHeldChanged();

private:
    // One of the glow's values over a breath, from `rest` to `peak` and back.
    [[nodiscard]] qreal breath(qreal phase, qreal rest, qreal peak) const;

    QEasingCurve m_standard = cubicBezier(0.2, 0, 0, 1);
    QEasingCurve m_flight = cubicBezier(0.2, 0.75, 0.25, 1);
    QEasingCurve m_queueArc = cubicBezier(0.45, 0, 0.35, 1);
    QEasingCurve m_artistPop = cubicBezier(0.3, 1.3, 0.5, 1);
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
    bool m_windowShown = true;
    bool m_windowMoving = false;
    bool m_windowActive = true;
};

} // namespace lmp::ui
