#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QTest>

#include "ui/icons/IconImageProvider.h"

using lmp::ui::IconImageProvider;

// Loading covers' glyphs: the app's own icons drawn as pictures, line work
// only in the colour asked for.
class IconImageProviderTest : public QObject {
    Q_OBJECT

private slots:
    void outlineStrokesWhatTheIconFills()
    {
        const QByteArray plain = IconImageProvider::svgFor(QStringLiteral("music-note/a9d6df"));
        const QByteArray outline = IconImageProvider::svgFor(QStringLiteral("music-note/a9d6df/outline"));
        QVERIFY(plain.contains(R"(fill="#a9d6df")"));
        QVERIFY(!outline.contains(R"(fill="#a9d6df")"));
        QVERIFY(outline.contains(R"(stroke="#a9d6df")"));
    }

    void drawsAtTheAskedSize()
    {
        IconImageProvider provider;
        QSize size;
        const QImage image = provider.requestImage(QStringLiteral("person/a9d6df/outline"), &size, QSize(40, 40));
        QCOMPARE(image.size(), QSize(40, 40));
        QCOMPARE(size, QSize(40, 40));
        int inked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                inked += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
            }
        }
        QVERIFY(inked > 40);
    }

    void unknownIconsAndColoursGiveNothing()
    {
        IconImageProvider provider;
        QSize size;
        QVERIFY(provider.requestImage(QStringLiteral("no-such-icon/a9d6df"), &size, QSize(20, 20)).isNull());
        QVERIFY(provider.requestImage(QStringLiteral("person/zz"), &size, QSize(20, 20)).isNull());
        QVERIFY(provider.requestImage(QStringLiteral("person"), &size, QSize(20, 20)).isNull());
    }
};

QTEST_MAIN(IconImageProviderTest)
#include "tst_icon_image_provider.moc"
