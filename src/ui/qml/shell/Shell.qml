import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// The application frame (index.html 47-142; styles.css 241-248, 1175-1191,
// 1785-1819): the left sidebar running the full height above the player bar,
// the title strip beside it, the content panel, the queue rail, and the
// player bar along the bottom. App.layout decides the widths; below 1180px the
// rail opens over the content instead of taking a column.
Item {
    id: shell

    readonly property int titleHeight: App.windowChrome.custom ? Theme.titleBarHeight : 0
    readonly property int bodyHeight: height - Theme.playerBarHeight
    readonly property bool railInColumn: App.layout.railDocked && App.layout.railOpen

    onWidthChanged: App.layout.windowWidth = width
    Component.onCompleted: App.layout.windowWidth = width

    // Left sidebar slot: the full height of the body, beside the title strip.
    Rectangle {
        id: sidebarSlot

        width: App.layout.sidebarWidth
        height: shell.bodyHeight
        color: Theme.surface
    }

    TitleStrip {
        x: sidebarSlot.width
        width: shell.width - sidebarSlot.width
        visible: App.windowChrome.custom
        z: 2
    }

    // The content panel sits 8px below the title strip.
    Rectangle {
        id: contentSlot

        x: sidebarSlot.width
        y: shell.titleHeight + 8
        width: shell.width - sidebarSlot.width - (shell.railInColumn ? App.layout.railWidth : 0)
        height: shell.bodyHeight - y
        // border-radius: 16px 16px 0 0
        topLeftRadius: Theme.radius2xl
        topRightRadius: Theme.radius2xl
        color: Theme.panelGrad1

        PageHost {
            anchors.fill: parent
        }
    }

    // The rail as a column of the layout.
    Rectangle {
        id: dockedRail

        x: shell.width - width
        y: shell.titleHeight
        width: App.layout.railWidth
        height: shell.bodyHeight - y
        visible: shell.railInColumn
        color: Theme.surface
    }

    // The rail over the content in narrower windows: from the top of the
    // window down to the player bar, with a soft shadow to its left.
    Item {
        id: overlayRail

        x: shell.width - width
        width: App.layout.railOverlayWidth
        height: shell.bodyHeight
        visible: !App.layout.railDocked && App.layout.railOpen
        z: 80

        RectangularShadow {
            anchors.fill: parent
            offset.x: -12
            blur: 36
            color: Theme.railShadow
        }

        Rectangle {
            anchors.fill: parent
            color: Theme.surface

            Rectangle {
                width: 1
                height: parent.height
                color: Theme.divider
            }
        }
    }

    // Player bar slot.
    Rectangle {
        y: shell.bodyHeight
        width: shell.width
        height: Theme.playerBarHeight
        color: Theme.surface
    }
}
