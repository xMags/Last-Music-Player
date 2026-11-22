pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// An action menu (floating-menu.ts; styles.css 885-978): a surface with an
// icon column, group dividers and a short drop-in, kept 8px inside the window.
// Arrow keys, Home and End move between enabled items, Enter picks one, Tab and
// Escape close it. A press outside closes it, so only one is open at a time.
T.Popup {
    id: menu

    required property MenuItemListModel items
    property int currentIndex: -1
    // The keyboard is driving the menu: show the focus ring on the current item.
    property bool keyboardActive: false

    signal triggered(string itemId)

    // Opens with the top-left corner at (x, y) in the menu's parent.
    function openAt(px, py) {
        keyboardActive = false
        x = px
        y = py
        open()
    }

    // Opens over `item`, as the keyboard's menu key does.
    function openBelow(item) {
        const point = item.mapToItem(parent, 12, 12)
        x = point.x
        y = point.y
        keyboardActive = true
        open()
    }

    function pick(row) {
        const id = items.idAt(row)
        close()
        triggered(id)
    }

    margins: 8
    padding: 4
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    transformOrigin: T.Popup.TopLeft
    // width: max-content, between 212px and 300px.
    implicitWidth: Math.min(300, Math.max(212, column.implicitWidth + leftPadding + rightPadding))
    implicitHeight: column.implicitHeight + topPadding + bottomPadding

    onOpened: currentIndex = items.firstEnabled()

    enter: Transition {
        enabled: !Motion.reduced
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.menuDuration; easing: Motion.menu }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Motion.menuDuration; easing: Motion.menu }
        NumberAnimation { target: shift; property: "y"; from: -6; to: 0; duration: Motion.menuDuration; easing: Motion.menu }
    }

    background: Item {
        transform: Translate { id: shift }

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowMenu
            radius: Theme.radiusMd
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.mixSrgb(Theme.surface, "transparent", 0.97)
            border.width: 1
            border.color: Theme.mixSrgb(Theme.fg, "transparent", 0.10)
        }
    }

    contentItem: Item {
        implicitWidth: column.implicitWidth
        implicitHeight: column.implicitHeight
        transform: Translate { y: shift.y }

        Keys.onPressed: event => {
            menu.keyboardActive = true
            if (event.key === Qt.Key_Down)
                menu.currentIndex = menu.items.nextEnabled(menu.currentIndex)
            else if (event.key === Qt.Key_Up)
                menu.currentIndex = menu.items.previousEnabled(menu.currentIndex)
            else if (event.key === Qt.Key_Home)
                menu.currentIndex = menu.items.firstEnabled()
            else if (event.key === Qt.Key_End)
                menu.currentIndex = menu.items.lastEnabled()
            else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)
                menu.close()
            else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) && menu.currentIndex >= 0)
                menu.pick(menu.currentIndex)
            else
                return
            event.accepted = true
        }
        focus: true

        Column {
            id: column

            width: parent.width

            Repeater {
                model: menu.items

                ActionMenuItem {
                    id: entry

                    width: column.width
                    focusShown: entry.index === menu.currentIndex && menu.keyboardActive
                    onPicked: menu.pick(entry.index)
                }
            }
        }
    }
}
