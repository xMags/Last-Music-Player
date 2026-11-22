#include <QByteArray>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <memory>

#include "ui/shell/PlaybackKeyGuard.h"

using lmp::ui::PlaybackKeyGuard;

namespace {

// Items of each kind the guard tells apart, found by objectName.
constexpr char Scene[] = R"(
import QtQuick
import QtQuick.Templates as T

Item {
    Item { objectName: "plain" }
    TextInput { objectName: "textInput" }
    TextEdit { objectName: "textEdit" }
    T.Button {
        objectName: "button"
        contentItem: Item { objectName: "insideButton" }
    }
    T.Slider { objectName: "slider" }
    T.ComboBox { objectName: "comboBox" }
    Item {
        objectName: "row"
        Accessible.role: Accessible.Button
    }
    Item {
        objectName: "listItem"
        Accessible.role: Accessible.ListItem
    }
}
)";

} // namespace

// Whether the window-wide playback keys leave a key to the focused item.
class PlaybackKeyGuardTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void leavesTheKeysToItemsThatUseThem();
    void takesTheKeysEverywhereElse();
    void blockedFollowsTheFocusItem();

private:
    [[nodiscard]] QQuickItem* item(const char* name) const;

    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_scene;
};

void PlaybackKeyGuardTest::initTestCase()
{
    QQmlComponent component(&m_engine);
    // A local address, so the imports are resolved at once rather than loaded.
    component.setData(QByteArray(Scene), QUrl::fromLocalFile(QStringLiteral("C:/scene/Scene.qml")));
    m_scene.reset(component.create());
    QVERIFY2(m_scene != nullptr, qPrintable(component.errorString()));
}

QQuickItem* PlaybackKeyGuardTest::item(const char* name) const
{
    return m_scene->findChild<QQuickItem*>(QString::fromLatin1(name));
}

void PlaybackKeyGuardTest::leavesTheKeysToItemsThatUseThem()
{
    for (const char* name : {"textInput", "textEdit", "button", "insideButton", "slider", "comboBox", "row"}) {
        QQuickItem* target = item(name);
        QVERIFY2(target != nullptr, name);
        QVERIFY2(PlaybackKeyGuard::ownsPlaybackKeys(target), name);
    }
}

void PlaybackKeyGuardTest::takesTheKeysEverywhereElse()
{
    QVERIFY(!PlaybackKeyGuard::ownsPlaybackKeys(nullptr));
    QVERIFY(!PlaybackKeyGuard::ownsPlaybackKeys(qobject_cast<QQuickItem*>(m_scene.get())));
    QVERIFY(!PlaybackKeyGuard::ownsPlaybackKeys(item("plain")));
    QVERIFY(!PlaybackKeyGuard::ownsPlaybackKeys(item("listItem")));
}

void PlaybackKeyGuardTest::blockedFollowsTheFocusItem()
{
    PlaybackKeyGuard guard;
    QSignalSpy blockedSpy(&guard, &PlaybackKeyGuard::blockedChanged);
    QVERIFY(!guard.blocked());

    guard.setFocusItem(item("textInput"));
    QVERIFY(guard.blocked());
    guard.setFocusItem(item("button"));
    QVERIFY(guard.blocked());
    QCOMPARE(blockedSpy.count(), 1);
    guard.setFocusItem(item("plain"));
    QVERIFY(!guard.blocked());
    guard.setFocusItem(nullptr);
    QVERIFY(!guard.blocked());
    QCOMPARE(blockedSpy.count(), 2);
}

QTEST_MAIN(PlaybackKeyGuardTest)

#include "tst_playback_key_guard.moc"
