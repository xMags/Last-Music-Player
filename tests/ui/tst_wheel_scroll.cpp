#include <QGuiApplication>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(LastMusicPlayerPlugin)

namespace {

// A page that scrolls, with a list inside it at the top that scrolls too.
constexpr char PageQml[] = R"(
import QtQuick
import LastMusicPlayer
Flickable {
    id: page
    width: 200
    height: 200
    contentHeight: 2000
    WheelScroll { flickable: page }
    Flickable {
        id: list
        objectName: "list"
        width: 200
        height: 100
        contentHeight: 300
        WheelScroll { flickable: list }
    }
}
)";

void turnWheel(QQuickWindow& window, QPointF at, int notches)
{
    QWheelEvent event(at, window.mapToGlobal(at), QPoint(), QPoint(0, -120 * notches), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(&window, &event);
}

} // namespace

// Mouse-wheel scrolling: 100px a notch, the inner view first, then the outer.
class WheelScrollTest : public QObject {
    Q_OBJECT

private slots:
    void aNotchScrollsTheViewUnderThePointerThenTheOneOutside()
    {
        QQmlEngine engine;
        QQuickWindow window; // never shown
        window.resize(200, 200);
        QQmlComponent component(&engine);
        component.setData(PageQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(page);
        page->setParentItem(window.contentItem());
        auto* list = page->findChild<QQuickItem*>(QStringLiteral("list"));
        QVERIFY(list);

        // Over the list: the list moves a notch, the page stays.
        turnWheel(window, QPointF(50, 50), 1);
        QTRY_COMPARE(list->property("contentY").toReal(), 100.0);
        QCOMPARE(page->property("contentY").toReal(), 0.0);

        // The list stops at its end (300 - 100).
        turnWheel(window, QPointF(50, 50), 2);
        QTRY_COMPARE(list->property("contentY").toReal(), 200.0);
        QCOMPARE(page->property("contentY").toReal(), 0.0);

        // At its end, the wheel passes to the page.
        turnWheel(window, QPointF(50, 50), 1);
        QTRY_COMPARE(page->property("contentY").toReal(), 100.0);
        QCOMPARE(list->property("contentY").toReal(), 200.0);

        // Back up over the page (below the list), and not past the top.
        turnWheel(window, QPointF(50, 150), -3);
        QTRY_COMPARE(page->property("contentY").toReal(), 0.0);
    }
};

QTEST_MAIN(WheelScrollTest)
#include "tst_wheel_scroll.moc"
