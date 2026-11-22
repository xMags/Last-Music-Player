pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// The application frame (index.html 47-142; styles.css 241-248, 1175-1191,
// 1785-1819): the left sidebar running the full height above the player bar,
// the title strip beside it, the content panel, the queue rail, and the
// player bar along the bottom. App.layout decides the widths; below 1180px the
// rail opens over the content instead of taking a column.
//
// The full-screen player covers all of it; the page behind is hidden then,
// so neither the pointer nor Tab reaches it, while the title strip stays on
// top across the whole width (styles.css 2311-2322). F opens and closes the
// player, Escape closes it; neither acts while a text field has the keys.
Item {
    id: shell

    readonly property int titleHeight: App.windowChrome.custom ? Theme.titleBarHeight : 0
    readonly property int bodyHeight: height - Theme.playerBarHeight
    readonly property bool railInColumn: App.layout.railDocked && App.layout.railOpen
    readonly property bool typing: Window.activeFocusItem instanceof TextInput || Window.activeFocusItem instanceof TextEdit
    // The rail's tab, shared by its docked and overlay forms.
    property int railTab: 0
    // Where keyboard focus was before the full-screen player opened.
    property Item focusBeforePlayer: null

    onWidthChanged: App.layout.windowWidth = width
    Component.onCompleted: App.layout.windowWidth = width

    Connections {
        target: App.fullscreen

        function onOpenChanged() {
            if (App.fullscreen.open)
                shell.focusBeforePlayer = shell.Window.activeFocusItem
            else if (shell.focusBeforePlayer)
                shell.focusBeforePlayer.forceActiveFocus(Qt.OtherFocusReason)
        }
    }

    Shortcut {
        sequence: "F"
        enabled: !shell.typing
        onActivated: App.fullscreen.toggle()
    }

    Shortcut {
        sequence: "Escape"
        enabled: App.fullscreen.open && !shell.typing
        onActivated: App.fullscreen.close()
    }

    Item {
        id: page

        anchors.fill: parent
        visible: !App.fullscreen.open

        // The left sidebar: the full height of the body, beside the title strip.
        Sidebar {
            id: sidebarSlot

            width: App.layout.sidebarWidth
            height: shell.bodyHeight
        }

        // The content panel sits 8px below the title strip.
        ContentPanel {
            id: contentSlot

            x: sidebarSlot.width
            y: shell.titleHeight + 8
            width: shell.width - sidebarSlot.width - (shell.railInColumn ? App.layout.railWidth : 0)
            height: shell.bodyHeight - y
            playing: App.playerBar.playing

            PageHost {
                anchors.fill: parent
            }
        }

        // The rail as a column of the layout.
        RightRail {
            id: dockedRail

            x: shell.width - width
            y: shell.titleHeight
            width: App.layout.railWidth
            height: shell.bodyHeight - y
            visible: shell.railInColumn
            tab: shell.railTab
            onTabChanged: shell.railTab = tab
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

            RightRail {
                anchors.fill: parent
                tab: shell.railTab
                onTabChanged: shell.railTab = tab

                // border-left: 1px solid divider
                Rectangle {
                    width: 1
                    height: parent.height
                    color: Theme.divider
                }
            }
        }

        PlayerBar {
            y: shell.bodyHeight
            width: shell.width
            height: Theme.playerBarHeight
        }
    }

    Loader {
        anchors.fill: parent
        active: App.fullscreen.open
        z: 90

        sourceComponent: FullscreenPlayer {
            titleHeight: shell.titleHeight
        }
    }

    TitleStrip {
        x: App.fullscreen.open ? 0 : sidebarSlot.width
        width: shell.width - x
        visible: App.windowChrome.custom
        overPlayer: App.fullscreen.open
        z: 95
    }

    // Toasts stack upwards from 104px above the window's bottom.
    ToastStack {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 104
        queue: App.toasts
        z: 100
    }
}
