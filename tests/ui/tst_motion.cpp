#include <QEasingCurve>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QSignalSpy>
#include <QTest>
#include <QVariantList>

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
