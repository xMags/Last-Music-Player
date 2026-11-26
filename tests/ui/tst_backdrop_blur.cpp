#include <QByteArray>
#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QPoint>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QString>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <cstdlib>
#include <memory>

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

namespace {

// A plain coloured backdrop and, over it, a 100 x 60 surface with the menu's
// blur (48px, saturate 1.5, rounded).
constexpr char SceneQml[] = R"(
import QtQuick
import LastMusicPlayer
Item {
    width: 400
    height: 300
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: "#2050a0"
    }
    Item {
        x: 150
        y: 120
        width: 100
        height: 60
        BackdropBlur {
            source: backdrop
            blurPx: 48
            saturate: 1.5
            cornerRadius: 8
        }
    }
}
)";

[[nodiscard]] bool near(QColor a, QColor b)
{
    return std::abs(a.red() - b.red()) <= 2 && std::abs(a.green() - b.green()) <= 2 && std::abs(a.blue() - b.blue()) <= 2;
}

} // namespace

// A blurred surface draws only inside its own outline: the page around it
// stays exactly as it is.
class BackdropBlurTest : public QObject {
    Q_OBJECT

private slots:
    void drawsNothingOutsideItsSurface()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(SceneQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* scene = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(scene);
        QQuickWindow window;
        window.resize(400, 300);
        scene->setParentItem(window.contentItem());

        const QImage image = window.grabWindow();
        QVERIFY(!image.isNull());
        const qreal scale = image.width() / 400.0;
        const auto at = [&image, scale](int x, int y) {
            return image.pixelColor(qRound(x * scale), qRound(y * scale));
        };
        const QColor page(0x20, 0x50, 0xa0);
        // Inside: the blurred, saturated backdrop, so not the page colour.
        QVERIFY2(!near(at(200, 150), page), qPrintable(at(200, 150).name()));
        // Around it, at 2 to 60 px from each edge: untouched.
        for (const int gap : {2, 10, 30, 60}) {
            for (const QPoint point : {QPoint(150 - gap, 150), QPoint(250 + gap, 150), QPoint(200, 120 - gap),
                                       QPoint(200, 180 + gap), QPoint(150 - gap, 120 - gap), QPoint(250 + gap, 120 - gap)}) {
                QVERIFY2(near(at(point.x(), point.y()), page),
                         qPrintable(QStringLiteral("%1,%2 is %3").arg(point.x()).arg(point.y()).arg(at(point.x(), point.y()).name())));
            }
        }
    }
};

// The blur is a shader effect, which the offscreen platform's software
// renderer does not draw; the window is never shown, so the Windows platform
// renders it offscreen with the real graphics API.
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows"));
    QGuiApplication app(argc, argv);
    BackdropBlurTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}
#include "tst_backdrop_blur.moc"
