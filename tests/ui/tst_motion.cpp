#include <QEasingCurve>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSignalSpy>
#include <QTest>
#include <QVariantList>

#include <cmath>

#include "ui/theme/Motion.h"
#include "ui/theme/ShadowLayer.h"
#include "ui/theme/Theme.h"

using lmp::ui::Motion;

namespace {

// QEasingCurve stores a bezier segment as: control 1, control 2, end point.
bool hasControlPoints(const QEasingCurve& curve, QPointF first, QPointF second)
{
    const QList<QPointF> points = curve.toCubicSpline();
    return curve.type() == QEasingCurve::BezierSpline && points.size() == 3
        && qFuzzyCompare(points.at(0).x() + 1, first.x() + 1)
        && qFuzzyCompare(points.at(0).y() + 1, first.y() + 1)
        && qFuzzyCompare(points.at(1).x() + 1, second.x() + 1)
        && qFuzzyCompare(points.at(1).y() + 1, second.y() + 1)
        && points.at(2) == QPointF(1, 1);
}

} // namespace

class MotionTest : public QObject {
    Q_OBJECT

private slots:
    void curvesMatchReference();
    void durationsMatchReference();
    void reducedNotifiesOnChangeOnly();
    void rippleReachesTheFarthestCorner();
    void toastEnterFollowsItsKeyframes();
    void songChangeMatchesReference();
    void layoutTokensMatchReference();
    void shadowsMatchReference();
};

void MotionTest::curvesMatchReference()
{
    const Motion motion;
    // --ease: cubic-bezier(0.2, 0, 0, 1)
    QVERIFY(hasControlPoints(motion.standard(), {0.2, 0}, {0, 1}));
    // --ease-water: cubic-bezier(0.22, 0.61, 0.36, 1)
    QVERIFY(hasControlPoints(motion.water(), {0.22, 0.61}, {0.36, 1}));
    // Toast spring: cubic-bezier(0.34, 1.45, 0.5, 1)
    QVERIFY(hasControlPoints(motion.toastSpring(), {0.34, 1.45}, {0.5, 1}));
    // Action menu: cubic-bezier(0.1, 0.9, 0.2, 1)
    QVERIFY(hasControlPoints(motion.menu(), {0.1, 0.9}, {0.2, 1}));
    // The CSS keyword `ease`: cubic-bezier(0.25, 0.1, 0.25, 1)
    QVERIFY(hasControlPoints(motion.ease(), {0.25, 0.1}, {0.25, 1}));
    // segment-glide.ts: cubic-bezier(0.3, 0.7, 0.2, 1)
    QVERIFY(hasControlPoints(motion.segment(), {0.3, 0.7}, {0.2, 1}));
    // .seek-bubble scale: cubic-bezier(0.3, 1.4, 0.5, 1)
    QVERIFY(hasControlPoints(motion.bubblePop(), {0.3, 1.4}, {0.5, 1}));
}

void MotionTest::durationsMatchReference()
{
    const Motion motion;
    QCOMPARE(motion.page(), 280);
    QCOMPARE(motion.menuDuration(), 170);
    QCOMPARE(motion.modal(), 160);
    QCOMPARE(motion.ripple(), 620);
    QCOMPARE(motion.segmentGlide(), 260);
    QCOMPARE(motion.heart(), 640);
    QCOMPARE(motion.artCrossfade(), 320);
    QCOMPARE(motion.ambient(), 1200);
    QCOMPARE(motion.ambientDrift(), 26000);
    QCOMPARE(motion.ambientFade(), 1400);
    QCOMPARE(motion.rippleRing(), 560);
    QCOMPARE(motion.rippleRingDelay(), 60);
    QCOMPARE(motion.controlFade(), 120);
    QCOMPARE(motion.chipFade(), 150);
    QCOMPARE(motion.playPress(), 100);
    QCOMPARE(motion.sliderGlide(), 140);
    QCOMPARE(motion.sliderThumb(), 100);
    QCOMPARE(motion.bubbleFade(), 120);
    QCOMPARE(motion.bubbleScale(), 160);
    QCOMPARE(motion.switchToggle(), 160);
    QCOMPARE(motion.popupOpen(), 120);
    QCOMPARE(motion.chevronTurn(), 150);
    QCOMPARE(motion.searchCommitDelay(), 350);
    QCOMPARE(motion.menuItemFade(), 80);
    QCOMPARE(motion.toastEnter(), 420);
    QCOMPARE(motion.toastExit(), 200);
    QCOMPARE(motion.toastReflow(), 280);
    QCOMPARE(motion.skeletonSweep(), 2800);
    QCOMPARE(motion.artRevealTimeout(), 2500);
    QCOMPARE(motion.navItemFade(), 100);
    QCOMPARE(motion.logoRipple(), 1900);
    QCOMPARE(motion.logoRippleStep(), 160);
}

void MotionTest::songChangeMatchesReference()
{
    const Motion motion;
    // song-change-motion.ts: the copy leaves in 220ms (ease-in), the new
    // text rises 10px in 320ms after 90ms, the cover settles in 380ms.
    QCOMPARE(motion.songChangeOut(), 220);
    QCOMPARE(motion.songChangeIn(), 320);
    QCOMPARE(motion.songChangeInDelay(), 90);
    QCOMPARE(motion.songChangeCover(), 380);
    QCOMPARE(motion.songChangeShift(), 10);
    // Cover keyframes: scale 0.9, 1.04 at 60%, then 1.
    QCOMPARE(motion.songChangeCoverScale(0), 0.9);
    QCOMPARE(motion.songChangeCoverScale(0.3), 0.97);
    QCOMPARE(motion.songChangeCoverScale(0.6), 1.04);
    QCOMPARE(motion.songChangeCoverScale(0.8), 1.02);
    QCOMPARE(motion.songChangeCoverScale(1), 1.0);
}

void MotionTest::reducedNotifiesOnChangeOnly()
{
    Motion motion;
    QSignalSpy spy(&motion, &Motion::reducedChanged);
    QVERIFY(!motion.reduced());
    motion.setReduced(true);
    motion.setReduced(true);
    QVERIFY(motion.reduced());
    motion.setReduced(false);
    QCOMPARE(spy.count(), 2);
}

void MotionTest::rippleReachesTheFarthestCorner()
{
    const Motion motion;
    // Pressed at (10, 10) in a 100 x 40 box: the farthest corner is
    // (100, 40), 90 across and 30 down; twice the distance.
    QCOMPARE(motion.rippleDiameter(100, 40, 10, 10), 2 * std::hypot(90.0, 30.0));
    // Pressed in the centre of a 30 x 40 box: 2 * hypot(15, 20) = 50.
    QCOMPARE(motion.rippleDiameter(30, 40, 15, 20), 50.0);
}

void MotionTest::toastEnterFollowsItsKeyframes()
{
    const Motion motion;
    // Keyframes: 0 -> (opacity 0, 26px, 0.92); 0.7 -> (1, -3px, 1.01); 1 -> (1, 0, 1).
    QCOMPARE(motion.toastEnterOpacity(0), 0.0);
    QCOMPARE(motion.toastEnterOffset(0), 26.0);
    QCOMPARE(motion.toastEnterScale(0), 0.92);
    QCOMPARE(motion.toastEnterOpacity(0.35), 0.5);
    QCOMPARE(motion.toastEnterOffset(0.7), -3.0);
    QCOMPARE(motion.toastEnterScale(0.7), 1.01);
    QCOMPARE(motion.toastEnterOpacity(1), 1.0);
    QCOMPARE(motion.toastEnterOffset(1), 0.0);
    QCOMPARE(motion.toastEnterScale(1), 1.0);
    // The spring overshoots 1: the last segment continues (+10px per unit),
    // and opacity stays at 1.
    QVERIFY(qFuzzyCompare(motion.toastEnterOffset(1.03) + 1, 0.3 + 1));
    QCOMPARE(motion.toastEnterOpacity(1.03), 1.0);
}

void MotionTest::layoutTokensMatchReference()
{
    const lmp::ui::Theme theme;
    QCOMPARE(theme.radiusSm(), 6);
    QCOMPARE(theme.radiusMd(), 8);
    QCOMPARE(theme.radiusLg(), 12);
    QCOMPARE(theme.radiusXl(), 14);
    QCOMPARE(theme.radius2xl(), 16);
    QCOMPARE(theme.playerBarHeight(), 90);
    QCOMPARE(theme.sidebarLeftWidth(), 240);
    QCOMPARE(theme.sidebarRightWidth(), 300);
    QCOMPARE(theme.sidebarLeftWidthWide(), 300);
    QCOMPARE(theme.sidebarRightWidthWide(), 380);
    QCOMPARE(theme.wideWindowMinWidth(), 1500);
    QCOMPARE(theme.titleBarHeight(), 32);
}

void MotionTest::shadowsMatchReference()
{
    lmp::ui::Theme theme;
    theme.setMode(lmp::ui::Theme::ThemeMode::Light);
    const QVariantList art = theme.shadowArt();
    QCOMPARE(art.size(), 3);
    const auto middle = art.at(1).value<lmp::ui::ShadowLayer>();
    QCOMPARE(middle.offsetY, 12.0);
    QCOMPARE(middle.blur, 28.0);
    QCOMPARE(middle.color.alpha(), qRound(0.14 * 255));

    theme.setMode(lmp::ui::Theme::ThemeMode::Dark);
    const auto darkCard = theme.shadowCard().at(0).value<lmp::ui::ShadowLayer>();
    QCOMPARE(darkCard.offsetY, 2.0);
    QCOMPARE(darkCard.blur, 6.0);
    QCOMPARE(darkCard.color.alpha(), qRound(0.36 * 255));
    QCOMPARE(theme.shadowFlyout().size(), 2);
}

QTEST_MAIN(MotionTest)
#include "tst_motion.moc"
