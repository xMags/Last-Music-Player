pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The Up Next rows (views/queue.ts 395-685; list-flip.ts), shared by the
// queue rail and the full-screen side panel. A click plays the row; a press
// that is held or moves up or down carries it to a new place (QueueRowDrag);
// right click, the "..." button, the Menu key and Shift+F10 open its menu.
// When the queue changes, rows glide to their new places, new rows slide in
// from the right one after another, and rows that left fade and shrink away.
//
// The list is 8px wider than its column on each side, as the rows reach
// 8px past the text (.queue-item margin 0 -8px); `listHeader` is laid out
// inside those 8px. `onDark` takes the full-screen player's colours.
Item {
    id: root

    property Component listHeader: null
    property bool onDark: false
    property bool showScrollBar: false
    property alias bottomMargin: list.bottomMargin

    readonly property color titleColor: onDark ? Theme.fullscreenText : Theme.fg
    readonly property color subColor: onDark ? Theme.fullscreenTertiaryText : Theme.fgTertiary
    readonly property color hoverColor: onDark ? Theme.fullscreenRowHover : Theme.hover

    ListView {
        id: list

        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: App.queueRail.queue

        T.ScrollBar.vertical: AppScrollBar {
            // Just outside the rows, in the panel's padding.
            parent: root
            x: root.width
            height: root.height
            visible: root.showScrollBar && size < 1
        }

        header: Item {
            // ListView keeps its position when the header changes height, which
            // left the list scrolled by the difference (the header's top cut
            // off). A list that was at the top stays at the top.
            property real shownHeight: 0

            width: list.width
            height: headerLoader.height
            onHeightChanged: { if (list.contentY <= 1 - shownHeight) list.contentY = -height; shownHeight = height }

            Loader {
                id: headerLoader

                x: 8
                width: parent.width - 2 * 8
                sourceComponent: root.listHeader
            }
        }

        footer: Item {
            width: list.width
            height: list.count === 0 ? empty.implicitHeight : 0

            CompactEmptyState {
                id: empty

                x: 8
                width: parent.width - 2 * 8
                visible: list.count === 0
                text: qsTr("Your queue is clear.")
                boxed: !root.onDark
                textColor: root.onDark ? Theme.fullscreenEmptyText : Theme.fgTertiary
            }
        }

        add: Transition {
            id: entering

            enabled: !Motion.reduced

            SequentialAnimation {
                PropertyAction { property: "opacity"; value: 0 }
                PropertyAction { property: "x"; value: Motion.listEnterShift }
                PauseAnimation { duration: Motion.listEnterDelay + (entering.ViewTransition.index - entering.ViewTransition.targetIndexes[0]) * Motion.listEnterStagger }
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; to: 1; duration: Motion.listGlide; easing: Motion.water }
                    NumberAnimation { property: "x"; to: 0; duration: Motion.listGlide; easing: Motion.water }
                }
            }
        }

        remove: Transition {
            enabled: !Motion.reduced

            NumberAnimation { property: "opacity"; to: 0; duration: Motion.listLeave; easing: Motion.easeIn }
            NumberAnimation { property: "scale"; to: Motion.listLeaveScale; duration: Motion.listLeave; easing: Motion.easeIn }
            NumberAnimation { property: "x"; to: -Motion.listLeaveShift; duration: Motion.listLeave; easing: Motion.easeIn }
        }

        move: Transition {
            enabled: !Motion.reduced

            NumberAnimation { property: "y"; duration: Motion.listGlide; easing: Motion.water }
        }

        // An interrupted entrance must not leave a row faded or shifted.
        displaced: Transition {
            enabled: !Motion.reduced

            NumberAnimation { property: "y"; duration: Motion.listGlide; easing: Motion.water }
            NumberAnimation { property: "opacity"; to: 1; duration: Motion.listGlide; easing: Motion.water }
            NumberAnimation { property: "x"; to: 0; duration: Motion.listGlide; easing: Motion.water }
            NumberAnimation { property: "scale"; to: 1; duration: Motion.listGlide; easing: Motion.water }
        }

        delegate: QueueRow {
            id: row

            width: list.width
            lifted: drag.dragging && drag.liftedRow === row.index
            carried: row.lifted ? drag.liftOffset : 0
            dropEdge: drag.dragging && !row.lifted && row.index === drag.targetRow ? (drag.targetRow < drag.liftedRow ? -1 : 1) : 0
            titleColor: root.titleColor
            subColor: root.subColor
            hoverColor: root.hoverColor
            onPressedAt: at => drag.press(row.index, row.rowKey, row.y, row.mapToItem(list.contentItem, at), row.mapToItem(list, at).y)
            onMovedTo: at => drag.move(row.mapToItem(list.contentItem, at), row.mapToItem(list, at).y)
            onReleased: drag.release()
            onCanceled: drag.cancel()
            onActivated: App.queueRail.playRow(row.index, row.rowKey)
            onMenuRequested: (at, fromKeyboard) => root.openMenu(row, at, fromKeyboard)
        }

        WheelScroll { flickable: list }
    }

    QueueRowDrag {
        id: drag

        rowHeight: 44 + 2 * 6
        rowCount: list.count
        viewportHeight: list.height
        onTapped: (row, rowKey) => App.queueRail.playRow(row, rowKey)
        onDropped: (from, to, rowKey) => App.queueRail.moveRow(from, to, rowKey)
    }

    // Near an edge while a row is carried, the list scrolls every frame.
    FrameAnimation {
        running: drag.scrollStep !== 0
        onTriggered: list.contentY = drag.scrolledContentY(list.contentY, list.originY, list.originY + list.contentHeight + list.bottomMargin - list.height)
    }

    ActionMenu {
        id: menu

        items: App.queueRail.menu
        onTriggered: itemId => App.queueRail.triggerMenu(itemId)
        onClosed: songMenus.disarmSoon()
    }

    // Where "Add to playlist" from a row's menu opens its playlist menu.
    TrackMenus {
        id: songMenus
    }

    function openMenu(row: QueueRow, at: point, fromKeyboard: bool) {
        if (!App.queueRail.prepareMenu(row.index, row.rowKey))
            return
        songMenus.arm(row, at.x, at.y)
        const place = row.mapToItem(root, at)
        menu.openAt(place.x, place.y)
        menu.keyboardActive = fromKeyboard
    }
}
