#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QSvgRenderer>
#include <QTest>

#include "ui/icons/IconSet.h"

using lmp::ui::IconPart;
using lmp::ui::IconSet;

class IconSetTest : public QObject {
    Q_OBJECT

private slots:
    void everyIconHasPathData();
    void namesAreUnique();
    void expectedIconsExist();
    void outlineIconsStrokeWithTheTextColour();
    void logosKeepTheirColours();
    void unknownNameDrawsNothing();
    void fillRulesGiveTheSameShape();
};

namespace {

// Renders one path filled black with the given SVG fill rule.
QImage renderFill(const QString& path, const char* rule)
{
    const QByteArray svg = QByteArray("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
                                      "<path fill='#000' fill-rule='")
        + rule + "' d='" + path.toLatin1() + "'/></svg>";
    QSvgRenderer renderer(svg);
    QImage image(96, 96, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    return image;
}

} // namespace

void IconSetTest::everyIconHasPathData()
{
    const IconSet icons;
    QVERIFY(icons.names().size() >= 130);
    for (const QString& name : icons.names()) {
        const int count = icons.partCount(name);
        QVERIFY2(count >= 1 && count <= IconSet::kMaxParts, qPrintable(name));
        for (int i = 0; i < count; ++i) {
            const IconPart part = icons.part(name, i);
            QVERIFY2(!part.path().trimmed().isEmpty(), qPrintable(name));
            QVERIFY2(part.filled() || part.stroked(), qPrintable(name));
        }
    }
}

void IconSetTest::namesAreUnique()
{
    const IconSet icons;
    const QStringList names = icons.names();
    const QSet<QString> unique(names.cbegin(), names.cend());
    QCOMPARE(unique.size(), names.size());
}

void IconSetTest::expectedIconsExist()
{
    const IconSet icons;
    const QStringList expected{
        QStringLiteral("play"),         QStringLiteral("pause"),
        QStringLiteral("heart"),        QStringLiteral("heart-fill"),
        QStringLiteral("queue"),        QStringLiteral("download-done"),
        QStringLiteral("nav-home"),     QStringLiteral("nav-home-active"),
        QStringLiteral("solid-share"),  QStringLiteral("logo-browser"),
        QStringLiteral("logo-phone"),   QStringLiteral("logo-desktop"),
    };
    for (const QString& name : expected) {
        QVERIFY2(icons.contains(name), qPrintable(name));
    }
    // Phone-only glyphs are left out.
    QVERIFY(!icons.contains(QStringLiteral("solid-favorite")));
}

void IconSetTest::outlineIconsStrokeWithTheTextColour()
{
    const IconSet icons;
    const IconPart heart = icons.part(QStringLiteral("heart"), 0);
    QVERIFY(heart.stroked());
    QVERIFY(!heart.filled());
    QCOMPARE(heart.strokeWidth(), 1.6);
    QCOMPARE(heart.capStyle(), static_cast<int>(Qt::RoundCap));
    QCOMPARE(heart.joinStyle(), static_cast<int>(Qt::RoundJoin));
    const QColor text(QStringLiteral("#123456"));
    QCOMPARE(heart.strokeFor(text), text);
    QCOMPARE(heart.fillFor(text).alpha(), 0);

    // play is a filled polygon without a stroke.
    const IconPart play = icons.part(QStringLiteral("play"), 0);
    QVERIFY(play.filled());
    QVERIFY(!play.stroked());
}

void IconSetTest::logosKeepTheirColours()
{
    const IconSet icons;
    QCOMPARE(icons.partCount(QStringLiteral("logo-browser")), 5);
    const IconPart pane = icons.part(QStringLiteral("logo-desktop"), 0);
    QCOMPARE(pane.fillFor(Qt::black), QColor(QStringLiteral("#0078D4")));
}

void IconSetTest::unknownNameDrawsNothing()
{
    const IconSet icons;
    QCOMPARE(icons.partCount(QStringLiteral("no-such-icon")), 0);
    const IconPart part = icons.part(QStringLiteral("no-such-icon"), 0);
    QVERIFY(part.path().isEmpty());
    QVERIFY(!part.filled() && !part.stroked());
    QVERIFY(icons.part(QStringLiteral("play"), 7).path().isEmpty());
}

// Parts are drawn with the odd-even rule unless they must keep winding
// (see IconPart::fillRule). That is only correct where both rules give the
// same shape, so render each such part both ways and compare.
void IconSetTest::fillRulesGiveTheSameShape()
{
    const IconSet icons;
    int checked = 0;
    for (const QString& name : icons.names()) {
        for (int i = 0; i < icons.partCount(name); ++i) {
            const IconPart part = icons.part(name, i);
            const bool switchedRule =
                !part.sourceEvenOdd() && part.fillRule() == static_cast<int>(Qt::OddEvenFill);
            if (!part.filled() || !switchedRule) {
                continue;
            }
            ++checked;
            QVERIFY2(renderFill(part.path(), "nonzero") == renderFill(part.path(), "evenodd"),
                     qPrintable(name + QLatin1Char('/') + QString::number(i)));
        }
    }
    QVERIFY(checked > 50);
}

QTEST_MAIN(IconSetTest)
#include "tst_icon_set.moc"
