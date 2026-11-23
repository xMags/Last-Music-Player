#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QTest>

#include <memory>

#include "ui/navigation/FocusPlace.h"

using lmp::ui::FocusPlace;

namespace {

// A page with two controls named "Play" and one named "Share", and a
// "Play" that is not a Tab stop.
constexpr char PageQml[] = R"(
import QtQuick
Item {
    Item { objectName: "first"; activeFocusOnTab: true; Accessible.name: "Play"; width: 10; height: 10 }
    Item {
        Item { objectName: "second"; activeFocusOnTab: true; Accessible.name: "Play"; width: 10; height: 10 }
        Item { objectName: "share"; activeFocusOnTab: true; Accessible.name: "Share"; width: 10; height: 10 }
    }
    Item { objectName: "label"; Accessible.name: "Play"; width: 10; height: 10 }
}
)";

} // namespace

// Finding a page's focused control again after the page was rebuilt.
class FocusPlaceTest : public QObject {
    Q_OBJECT

private slots:
    void findsControlsByNameAndPlace()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(PageQml, QUrl());
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* page = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(page);
        auto* first = page->findChild<QQuickItem*>(QStringLiteral("first"));
        auto* second = page->findChild<QQuickItem*>(QStringLiteral("second"));
        auto* share = page->findChild<QQuickItem*>(QStringLiteral("share"));
        auto* label = page->findChild<QQuickItem*>(QStringLiteral("label"));
        QVERIFY(first && second && share && label);

        FocusPlace place;
        QCOMPARE(place.nameOf(second), QStringLiteral("Play"));
        QCOMPARE(place.indexOf(page, first), 0);
        QCOMPARE(place.indexOf(page, second), 1);
        QCOMPARE(place.indexOf(page, share), 0);
        // Not a Tab stop: not a place focus can come back to.
        QCOMPARE(place.indexOf(page, label), -1);
        QCOMPARE(place.find(page, QStringLiteral("Play"), 1), second);
        QCOMPARE(place.find(page, QStringLiteral("Share"), 0), share);
        // Fewer controls than before: the first one with that name.
        QCOMPARE(place.find(page, QStringLiteral("Play"), 5), first);
        QCOMPARE(place.find(page, QStringLiteral("Gone"), 0), nullptr);
        QCOMPARE(place.indexOf(nullptr, first), -1);
        QVERIFY(!place.hasKeyboardFocus(first));
    }
};

QTEST_MAIN(FocusPlaceTest)
#include "tst_focus_place.moc"
