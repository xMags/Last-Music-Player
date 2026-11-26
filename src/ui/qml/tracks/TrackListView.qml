pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A song list in the saved layout (common.ts trackCollection): the track
// table (List) or song cards (Grid). Either way it scrolls itself, so only
// the visible rows or cards exist and a long list stays smooth; more pages
// load as it nears the end. `header` (the page above the list) scrolls with
// it. Enter and Space play the focused song; the arrow keys move.
//
// The table (.track-table, views.css 153-172): a 12px-rounded surface with
// a border and a shadow, its heading row (#, TITLE, ALBUM, TIME) on the
// alternate surface.
Item {
    id: root

    required property TrackListModel model
    property bool grid: false
    // A release table (catalog.ts releaseCollection): RELEASED and SONGS
    // for ALBUM and TIME, and no heart.
    property string albumHeading: qsTr("ALBUM")
    property string timeHeading: qsTr("TIME")
    property bool likes: true
    // Release rows: a "More" button in the heart's place.
    property bool moreButtons: false
    // Set when the list fills the content panel, so its scroll bar clears
    // the panel's rounded top corners (AppScrollBar.laneTopMargin).
    property real scrollLaneTopMargin: 0

    // Switching between List and Grid: the new form's rows or cards fade in
    // one after another (the plan's fade in place of the cover glide), and
    // the new form opens scrolled as far as the old one was, so the header
    // and the switch in it stay where they were.
    onGridChanged: {
        if (!root.formReady)
            return
        root.startOffset = root.view !== null ? root.offsetOf(root.view) : 0
        root.shownGrid = root.grid
        App.cascade.restart()
        // The new form's header may be at its full height from the start,
        // with no height change to place it, so place it here as well.
        root.holdStart = true
        Qt.callLater(root.placeViewAtStart)
    }
    property Component header: null
    // Space below the last row, inside the scrolling area.
    property real bottomMargin: 32
    // Space at each side of the rows; the scroll bar sits in the right one.
    property real sidePadding: 0
    // The right side, when it differs (the scrollbar lane).
    property real rightSidePadding: sidePadding
    // A collection page's extras (List only): Ctrl and Shift clicks select
    // instead of playing, the compact density, and dragging rows into a new
    // order. While a row is dragged the rows it passes make room for it.
    property bool selectable: false
    property bool compact: false
    property bool reorderable: false
    property int dragFrom: -1
    property real dragOffset: 0
    // The pointer's own movement and the list's scroll when the drag began;
    // the dragged row follows both (the list may scroll under it).
    property real dragPointerShift: 0
    property real dragStartContentY: 0
    readonly property int dragTarget: dragFrom < 0 ? -1
        : Math.max(0, Math.min(model.count - 1, Math.round(dragFrom + dragOffset / rowHeight)))

    signal activated(int row)
    // A Ctrl click (toggle) or a Shift click (extend) on a selectable list.
    signal selectRequested(int row, bool extend)
    // A drag is about to start (the page loads every row first).
    signal dragStarted()
    signal moveRequested(int from, int to)
    signal likeRequested(int row)
    // `at` is in this item's coordinates.
    signal menuRequested(int row, point at, bool fromKeyboard)

    // As TrackRow sizes itself: 41px compact, else 55px (.track-row).
    readonly property int rowHeight: compact ? 28 + 2 * 6 + 1
                                             : Math.max(36, Theme.lineHeightTrackTitle + Theme.lineHeightTrackArtist) + 2 * 8 + 1
    readonly property int headHeight: 41
    // The list or grid that scrolls, for keeping its position.
    readonly property Flickable view: form.item as Flickable

    // The form on show. It follows `grid` in onGridChanged, not by binding,
    // so the old form's offset is read before the Loader replaces it.
    property bool shownGrid: false
    property bool formReady: false
    // How far below the header's top a new form opens, and stays while its
    // header grows, until it is scrolled.
    property real startOffset: 0
    // Set while the form holds startOffset; cleared once it is scrolled.
    property bool holdStart: true

    Component.onCompleted: {
        root.shownGrid = root.grid
        root.formReady = true
    }

    // How far `flickable` is scrolled below the top of its header.
    function offsetOf(flickable: Flickable): real {
        return flickable.contentY - flickable.originY + flickable.topMargin
    }

    // Scrolls `flickable` to startOffset, or as far as its content reaches.
    function placeAtStart(flickable: Flickable) {
        const furthest = Math.max(0, flickable.contentHeight + flickable.topMargin + flickable.bottomMargin - flickable.height)
        flickable.contentY = flickable.originY - flickable.topMargin + Math.min(root.startOffset, furthest)
    }

    // Shows the list from `offset` below its header's top, holding there
    // while the header grows (a page switching to this list from another
    // view of the same items).
    function startAt(offset: real) {
        root.startOffset = offset
        const flickable = root.view
        if (flickable === null)
            return
        root.holdStart = true
        Qt.callLater(root.placeAtStart, flickable)
    }

    // A new route that keeps this page (one playlist, then another) opens
    // at the top and holds there while its header fills. Back leaves the
    // place to ScrollMemory. Deferred: when the route shows another kind of
    // page this one is gone by then and the call is dropped, so a list on
    // its way out never moves (its ScrollMemory would record the move in
    // the new route's entry).
    Connections {
        target: App.navigator

        function onCurrentChanged() {
            root.startOffset = 0
            root.holdStart = !App.navigator.returning
            if (root.holdStart)
                Qt.callLater(root.placeViewAtStart)
        }
    }

    function placeViewAtStart() {
        if (root.view !== null)
            root.placeAtStart(root.view)
    }

    Loader {
        id: form

        anchors.fill: parent
        anchors.leftMargin: root.sidePadding
        anchors.rightMargin: root.rightSidePadding
        active: root.formReady
        sourceComponent: root.shownGrid ? gridForm : listForm
    }

    Component {
        id: listForm

        ListView {
            id: list

            model: root.model
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            reuseItems: true
            bottomMargin: root.bottomMargin
            keyNavigationEnabled: true
            currentIndex: -1

            readonly property real rowWidth: width

            AppScrollBar.vertical: AppScrollBar {
                parent: root
                x: root.width - width
                height: root.height
                laneTopMargin: root.scrollLaneTopMargin
                onPressedChanged: {
                    if (pressed)
                        root.holdStart = false
                }
            }

            // Until the list is scrolled, it stays at its start (the top,
            // or where the other form was) while the header above grows (a
            // page fills its header after the list exists; a ListView would
            // otherwise keep its old offset and hide the header's top).
            // The wheel, the scroll bar and the arrow keys move the list
            // without a movementStarted, so they end the hold as well.
            Component.onCompleted: root.holdStart = true
            onMovementStarted: root.holdStart = false
            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    root.holdStart = false
            }

            header: Column {
                width: list.rowWidth
                // Deferred: the header can grow while the list is still
                // creating items, and repositioning then would re-enter it.
                onHeightChanged: {
                    if (root.holdStart)
                        Qt.callLater(root.placeAtStart, list)
                }

                Loader {
                    width: parent.width
                    sourceComponent: root.header
                }

                // The table's heading row, with the table's top corners.
                Item {
                    width: parent.width
                    height: list.count > 0 ? root.headHeight : 0
                    visible: list.count > 0

                    // Rounded at the top only: the lower corners fall
                    // outside the clip.
                    Item {
                        anchors.fill: parent
                        clip: true

                        Rectangle {
                            width: parent.width
                            height: parent.height + 12
                            radius: 12
                            color: Theme.surfaceAlt
                            border.width: 1
                            border.color: Theme.stroke
                        }
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        x: 1
                        width: parent.width - 2
                        height: 1
                        color: Theme.divider
                    }

                    // padding: 12px 20px; the rows' columns: 40px 1fr 200px
                    // 60px 40px, 16px apart (the last one has no heading).
                    Row {
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16

                        TableHeading { width: 40; text: "#"; horizontalAlignment: Text.AlignHCenter }
                        TableHeading {
                            width: list.rowWidth - 2 * 20 - (40 + 16) - (albumHeading.visible ? 200 + 16 : 0)
                                - (60 + 16) - 40
                            text: qsTr("TITLE")
                        }
                        TableHeading { id: albumHeading; width: 200; visible: list.rowWidth >= 560; text: root.albumHeading }
                        TableHeading { width: 60; text: root.timeHeading; horizontalAlignment: Text.AlignRight }
                    }
                }
            }

            WheelScroll {
                flickable: list
                onScrolled: root.holdStart = false
            }

            // .track-table box-shadow: var(--shadow), round the heading and
            // the body together.
            BoxShadow {
                parent: list.contentItem
                z: -2
                // The rows start at 0; the heading row sits just above them.
                y: -root.headHeight
                width: list.rowWidth
                height: list.count * root.rowHeight + root.headHeight
                visible: list.count > 0
                layers: Theme.shadow
                radius: 12
            }

            // The table's body behind the rows, with its bottom corners.
            Rectangle {
                parent: list.contentItem
                z: -1
                y: 0
                width: list.rowWidth
                height: list.count * root.rowHeight
                visible: list.count > 0
                color: Theme.surface
                border.width: 1
                border.color: Theme.stroke
                radius: 12

                // Square top corners: the heading row above has the rounded ones.
                Rectangle {
                    width: parent.width
                    height: 12
                    color: Theme.surface
                    border.width: 0

                    Rectangle { width: 1; height: parent.height; color: Theme.stroke }
                    Rectangle { x: parent.width - 1; width: 1; height: parent.height; color: Theme.stroke }
                }
            }

            delegate: TrackRow {
                id: row

                required property int index
                required selected
                // Where this row sits while another is dragged past it.
                readonly property real dragShift: {
                    const from = root.dragFrom
                    const to = root.dragTarget
                    if (from < 0 || row.index === from)
                        return 0
                    if (from < to && row.index > from && row.index <= to)
                        return -root.rowHeight
                    if (to < from && row.index >= to && row.index < from)
                        return root.rowHeight
                    return 0
                }

                width: list.rowWidth
                compact: root.compact
                showLike: root.likes
                showMore: root.moreButtons
                z: row.index === root.dragFrom ? 10 : 0
                transform: Translate {
                    y: row.index === root.dragFrom ? root.dragOffset : row.dragShift

                    Behavior on y {
                        enabled: row.index !== root.dragFrom && !Motion.reduced
                        NumberAnimation { duration: Motion.controlFade; easing.type: Easing.OutCubic }
                    }
                }
                onClicked: {
                    const toggle = (row.pressModifiers & Qt.ControlModifier) !== 0
                    const extend = (row.pressModifiers & Qt.ShiftModifier) !== 0
                    if (root.selectable && (toggle || extend))
                        root.selectRequested(row.index, extend)
                    else
                        root.activated(row.index)
                }
                onLikeRequested: root.likeRequested(row.index)
                onMenuRequested: (at, fromKeyboard) => root.menuRequested(row.index, row.mapToItem(root, at), fromKeyboard)
                Keys.onReturnPressed: root.activated(row.index)
                Keys.onEnterPressed: root.activated(row.index)

                DragHandler {
                    id: drag

                    enabled: root.reorderable
                    target: null
                    xAxis.enabled: false
                    onActiveChanged: {
                        if (drag.active) {
                            root.dragStarted()
                            root.dragPointerShift = 0
                            root.dragStartContentY = list.contentY
                            root.dragOffset = 0
                            root.dragFrom = row.index
                        } else if (root.dragFrom >= 0) {
                            const from = root.dragFrom
                            const to = root.dragTarget
                            root.dragFrom = -1
                            root.dragOffset = 0
                            if (to >= 0 && to !== from)
                                root.moveRequested(from, to)
                        }
                    }
                    onTranslationChanged: {
                        if (!drag.active)
                            return
                        root.dragPointerShift = drag.translation.y
                        root.dragOffset = root.dragPointerShift + list.contentY - root.dragStartContentY
                    }
                    onCentroidChanged: {
                        if (drag.active)
                            edgeScroll.pointerY = drag.centroid.scenePosition.y - list.mapToItem(null, 0, 0).y
                    }
                }
            }

            // Scrolls while a dragged row is held near the top or bottom edge.
            Timer {
                id: edgeScroll

                property real pointerY: 0
                readonly property real edge: 48

                interval: 16
                repeat: true
                running: root.dragFrom >= 0 && (pointerY < edge || pointerY > list.height - edge)
                onTriggered: {
                    const step = pointerY < edge ? -8 : 8
                    const top = list.originY - list.topMargin
                    const bottom = Math.max(top, list.contentHeight + list.originY + list.bottomMargin - list.height)
                    const next = Math.max(top, Math.min(bottom, list.contentY + step))
                    list.contentY = next
                    root.dragOffset = root.dragPointerShift + list.contentY - root.dragStartContentY
                }
            }
        }
    }

    Component {
        id: gridForm

        CardGrid {
            id: cards

            model: root.model
            bottomMargin: root.bottomMargin
            scrollBarHost: root
            scrollLaneTopMargin: root.scrollLaneTopMargin
            keyNavigationEnabled: true
            currentIndex: -1

            Component.onCompleted: root.holdStart = true
            onMovementStarted: root.holdStart = false
            onUserScrolled: root.holdStart = false
            onCurrentIndexChanged: {
                if (currentIndex >= 0)
                    root.holdStart = false
            }

            header: Loader {
                width: cards.width
                sourceComponent: root.header
                onHeightChanged: {
                    if (root.holdStart)
                        Qt.callLater(root.placeAtStart, cards)
                }
            }

            delegate: Item {
                id: cell

                required property int index
                required property string title
                required property string artist
                required property url artworkUrl
                required property string artworkId
                required property bool playing
                required property bool unavailable
                required property string unavailableReason
                required property string remoteUrl

                width: cards.cellWidth
                height: cards.cellHeight

                TrackTile {
                    width: cards.cardWidth
                    title: cell.title
                    artist: cell.artist
                    artworkUrl: cell.artworkUrl
                    artworkId: cell.artworkId
                    playing: cell.playing
                    unavailable: cell.unavailable
                    remoteUrl: cell.remoteUrl
                    unavailableReason: cell.unavailableReason
                    onClicked: root.activated(cell.index)
                    onMenuRequested: (at, fromKeyboard) => root.menuRequested(cell.index, mapToItem(root, at), fromKeyboard)
                    Keys.onReturnPressed: root.activated(cell.index)
                }
            }
        }
    }
}
