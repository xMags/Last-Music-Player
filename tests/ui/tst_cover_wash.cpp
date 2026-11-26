#include <QColor>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QTest>

#include "ui/images/CoverWash.h"

using lmp::ui::coverWash;
using lmp::ui::coverWashSize;
using lmp::ui::CoverWashOptions;

namespace {

QImage filled(QSize size, QColor color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

} // namespace

// A cover's colour wash: a tiny, softened, more saturated picture (the web's
// cover-wash.ts).
class CoverWashTest : public QObject {
    Q_OBJECT

private slots:
    void theWashHasTheSizeAskedFor();
    void greysStayGrey();
    void coloursAreSaturatedAsCssSaturateDoes();
    void aFlatCoverStaysFlatAndOpaque();
    void theWashSizeIsBounded();
};

void CoverWashTest::theWashHasTheSizeAskedFor()
{
    const QImage cover = filled(QSize(600, 300), QColor(200, 40, 40));
    QCOMPARE(coverWash(cover, {}).size(), QSize(16, 16));
    QCOMPARE(coverWash(cover, {.size = QSize(14, 5)}).size(), QSize(14, 5));
    QVERIFY(coverWash(QImage(), {}).isNull());
    QVERIFY(coverWash(cover, {.size = QSize(0, 4)}).isNull());
}

void CoverWashTest::greysStayGrey()
{
    const QImage wash = coverWash(filled(QSize(64, 64), QColor(120, 120, 120)), {});
    const QColor middle = wash.pixelColor(8, 8);
    QCOMPARE(middle.red(), 120);
    QCOMPARE(middle.green(), 120);
    QCOMPARE(middle.blue(), 120);
}

void CoverWashTest::coloursAreSaturatedAsCssSaturateDoes()
{
    // saturate(1.5) of rgb(150, 100, 100) by the Filter Effects matrix:
    // red   1.3935 * 150 - 0.3575 * 100 - 0.036 * 100 = 169.7
    // green -0.1065 * 150 + 1.1425 * 100 - 0.036 * 100 = 94.7
    // blue  -0.1065 * 150 - 0.3575 * 100 + 1.464 * 100 = 94.7
    const QImage wash = coverWash(filled(QSize(64, 64), QColor(150, 100, 100)), {});
    const QColor middle = wash.pixelColor(8, 8);
    QCOMPARE(middle.red(), 170);
    QCOMPARE(middle.green(), 95);
    QCOMPARE(middle.blue(), 95);
}

void CoverWashTest::aFlatCoverStaysFlatAndOpaque()
{
    // Most of the blur's faded rim is cut away: the corners keep about 3% of
    // it (transparent beyond the picture, as on the web's canvas).
    const QImage wash = coverWash(filled(QSize(40, 40), QColor(30, 90, 160)), {.saturation = 1.0});
    QCOMPARE(wash.pixelColor(8, 8).alpha(), 255);
    for (const QPoint corner : {QPoint(0, 0), QPoint(15, 0), QPoint(0, 15), QPoint(15, 15)}) {
        const QColor c = wash.pixelColor(corner);
        QVERIFY2(c.alpha() >= 240, qPrintable(QString::number(c.alpha())));
        QVERIFY(qAbs(c.red() - 30) <= 2 && qAbs(c.green() - 90) <= 2 && qAbs(c.blue() - 160) <= 2);
    }
}

void CoverWashTest::theWashSizeIsBounded()
{
    QCOMPARE(coverWashSize(QSize()), QSize(16, 16));
    QCOMPARE(coverWashSize(QSize(0, 0)), QSize(16, 16));
    QCOMPARE(coverWashSize(QSize(14, 5)), QSize(14, 5));
    QCOMPARE(coverWashSize(QSize(4000, 90)), QSize(64, 64));
}

QTEST_GUILESS_MAIN(CoverWashTest)
#include "tst_cover_wash.moc"
