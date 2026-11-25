pragma ComponentBehavior: Bound

import QtQuick
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
// Space plays and pauses, Ctrl+Right and Ctrl+Left skip; these three leave
// the keys to a focused text field, button, slider, drop-down or popup.
// Ctrl+K opens Browse with the caret in its search field.
// Alt+Left and the mouse's back button go back a page (the browser Back the
// reference relies on), except while the full-screen player is open.
Item {
    id: shell

    readonly property int titleHeight: App.windowChrome.custom ? Theme.titleBarHeight : 0
    readonly property int bodyHeight: height - Theme.playerBarHeight
    readonly property bool railInColumn: App.layout.railDocked && App.layout.railOpen
    readonly property bool typing: Window.activeFocusItem instanceof TextInput || Window.activeFocusItem instanceof TextEdit
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

    PlaybackKeyGuard {
        id: playbackKeys

        focusItem: shell.Window.activeFocusItem
    }

    Shortcut {
        sequence: "Space"
        enabled: !playbackKeys.blocked
        onActivated: App.playerBar.togglePlay()
    }

    Shortcut {
        sequence: "Ctrl+Right"
        enabled: !playbackKeys.blocked
        onActivated: App.playerBar.next()
    }

    Shortcut {
        sequence: "Ctrl+Left"
        enabled: !playbackKeys.blocked
        onActivated: App.playerBar.previous()
    }

    // Ctrl+K: Browse, with the caret in its search field.
    Shortcut {
        sequence: "Ctrl+K"
        enabled: !App.fullscreen.open
        onActivated: {
            if (App.navigator.currentKind !== Route.Browse)
                App.navigator.openView(Route.Browse)
            Qt.callLater(() => App.search.requestFocus())
        }
    }

    Shortcut {
        sequences: [StandardKey.Back]
        enabled: App.navigator.canGoBack && !App.fullscreen.open
        onActivated: App.navigator.back()
    }

    // Sees only the back button; every other click passes through.
    MouseArea {
        anchors.fill: parent
        z: 1000
        acceptedButtons: Qt.BackButton
        enabled: App.navigator.canGoBack && !App.fullscreen.open
        onClicked: App.navigator.back()
    }

    Item {
        id: page

        anchors.fill: parent
        visible: !App.fullscreen.open
        // What menus and dialog backdrops blur.
        Component.onCompleted: App.effects.windowBackdrop = page

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

            // A card's cover flying into the page it opened.
            SharedCover {
                anchors.fill: parent
                z: 10
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
            tab: App.layout.railTab
            onTabChanged: App.layout.railTab = tab
        }

        // The rail over the content in narrower windows: from the top of the
        // window down to the player bar, with a soft shadow to its left.
        // Fixed over everything, the title strip and its caption buttons
        // included (.sidebar-right: position fixed, top 0, z-index 80 over
        // the titlebar's 2), so its tabs are never cut.
        Item {
            id: overlayRail

            parent: shell
            x: shell.width - width
            y: 0
            width: App.layout.railOverlayWidth
            height: shell.bodyHeight
            visible: !App.layout.railDocked && App.layout.railOpen && !App.fullscreen.open
            z: 96

            AppRectangularShadow {
                anchors.fill: parent
                offset.x: -12
                blur: 36
                color: Theme.railShadow
            }

            RightRail {
                anchors.fill: parent
                tab: App.layout.railTab
                onTabChanged: App.layout.railTab = tab

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
        overWelcome: App.welcome.visible
        z: 95
    }

    // Started without reaching the account service (main.ts startOffline):
    // say so once and open the Offline page, where downloaded songs play.
    // The saved library stays in place meanwhile.
    property bool offlineStartShown: false

    Connections {
        target: App.connectivity

        function onOfflineChanged() {
            if (!App.connectivity.offline || shell.offlineStartShown)
                return
            shell.offlineStartShown = true
            App.toasts.show(qsTr("You're offline. Downloaded songs still play."))
            if (App.navigator.currentKind !== Route.Offline)
                App.navigator.openView(Route.Offline)
        }
    }

    // A new account's welcome flow, over everything but the title strip.
    Loader {
        anchors.fill: parent
        active: App.welcome.visible
        z: 92

        sourceComponent: WelcomeFlow {}
    }

    // New, rename and delete for playlists and albums (App.collections).
    CollectionDialogs {}

    // The one-time history import question after signing in.
    HistoryImportDialog {}

    // The queue arc, heart burst and save motion, over everything.
    MotionLayer {
        anchors.fill: parent
        z: 150
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
