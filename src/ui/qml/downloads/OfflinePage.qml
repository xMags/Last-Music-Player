pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The Offline page (App.offline; the reference's views/offline.ts): the
// songs downloaded on this PC with Library's tabs. Songs lists them all
// (Play, Shuffle, More, the usual rows) with the songs still on their way in
// a strip above; the other tabs show the listener's playlists, recent plays,
// albums, artists and genres, each cut down to what is downloaded, and a
// card opens as a collection page of its own. While the connection is down
// a notice says downloaded songs still play.
Item {
    id: page

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane
    readonly property bool groupOpen: App.offline.groupTitle !== ""
    readonly property bool songTab: App.offline.tab === "songs" || App.offline.tab === "history"

    Component.onCompleted: App.offline.refresh()

    Component {
        id: top

        Column {
            id: head

            // The card grid already keeps the gutters as its margins.
            readonly property bool inGrid: parent !== null && parent.parent === cardsView
            readonly property real inner: width - leftPadding - rightPadding

            width: inGrid ? cardsView.width : songs.width
            leftPadding: inGrid ? 0 : page.gutter
            rightPadding: inGrid ? 0 : page.rightGutter
            spacing: 0

            // An opened card: the header Library's collections use, and the
            // page's own title, notice and tabs step aside.
            CollectionHeader {
                visible: page.groupOpen
                width: head.inner
                backLabel: App.offline.groupBackLabel
                eyebrow: App.offline.groupEyebrow
                title: App.offline.groupTitle
                byline: App.offline.groupByline
                info: App.offline.groupInfo
                canPlay: App.offline.songs.count > 0
                showMenu: false
                coverStyle: App.offline.groupCover.style
                artworkUrl: App.offline.groupArtworkUrl
                artworkId: "offline:" + App.offline.groupTitle
                artworkLabel: App.offline.groupTitle
                gradientStart: App.offline.groupCover.gradientStart
                gradientEnd: App.offline.groupCover.gradientEnd
                iconName: App.offline.groupCover.iconName
                genre: App.offline.groupCover.style === "genre" ? App.offline.groupTitle : ""
                roundCover: App.offline.groupRound
                onBackClicked: App.offline.closeGroup()
                onPlayClicked: App.offline.play()
                onShuffleClicked: App.offline.shuffle()
            }

            Column {
                visible: !page.groupOpen
                width: head.inner
                // .view-header: padding 18px 0 22px.
                topPadding: 18
                spacing: 0

                RowLayout {
                    width: parent.width
                    spacing: 12

                    Column {
                        Layout.fillWidth: true
                        spacing: 0

                        Text {
                            width: parent.width
                            text: qsTr("Downloads")
                            font: Theme.viewTitle
                            color: Theme.fg
                            elide: Text.ElideRight
                        }

                        // .offline-summary: 6px under the title, 13px, tertiary.
                        Text {
                            visible: App.offline.summary !== ""
                            topPadding: 6
                            text: App.offline.summary
                            font: Theme.viewSubtitle
                            color: Theme.fgTertiary
                        }
                    }

                    // Kept from the current app (D10): hides finished jobs.
                    AppButton {
                        visible: App.offline.hasCompleted
                        text: qsTr("Clear completed")
                        onClicked: App.offline.clearCompleted()
                    }
                }

                Item { width: 1; height: 22 }

                // .offline-notice: padded 10px 14px, radius 10px, the
                // selection tint, 13px secondary text, an accent icon 10px
                // before it; 18px below.
                Rectangle {
                    width: parent.width
                    height: notice.implicitHeight + 2 * 10
                    radius: 10
                    visible: App.connectivity.offline
                    color: Theme.accentSel

                    Row {
                        id: notice

                        anchors.verticalCenter: parent.verticalCenter
                        x: 14
                        spacing: 10

                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "wifi-off"
                            size: 16
                            color: Theme.accentColor
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("You're offline. Downloaded songs still play.")
                            font: Theme.viewSubtitle
                            color: Theme.fgSecondary
                        }
                    }
                }

                Item { width: 1; height: 18; visible: App.connectivity.offline }

                // .offline-tabs: Library's tabs, 18px above what they show.
                SegmentedControl {
                    visible: App.offline.hasDownloads || App.offline.pendingShown
                    compact: true
                    choices: App.offline.tabs
                    currentIndex: Math.max(0, App.offline.tabKeys.indexOf(App.offline.tab))
                    onActivated: index => App.offline.tab = App.offline.tabKeys[index]
                }

                Item { width: 1; height: 18; visible: App.offline.hasDownloads || App.offline.pendingShown }

                // Downloads on their way (Songs only): PendingStrip below.
                PendingStrip {
                    width: parent.width
                    visible: App.offline.pendingShown && App.offline.tab === "songs"
                }

                Item {
                    width: 1
                    height: 18
                    visible: App.offline.pendingShown && App.offline.tab === "songs" && App.offline.songs.count > 0
                }

                // .offline-actions: Play, Shuffle, More (Songs only) and the
                // List / Grid switch at the end; 20px below.
                RowLayout {
                    width: parent.width
                    visible: page.songTab && App.offline.songs.count > 0
                    spacing: 10

                    PlayPill {
                        onClicked: App.offline.play()
                    }

                    RoundAction {
                        iconName: "shuffle"
                        toolTip: qsTr("Shuffle")
                        Accessible.name: qsTr("Shuffle")
                        onClicked: App.offline.shuffle()
                    }

                    RoundAction {
                        id: moreButton

                        visible: App.offline.tab === "songs"
                        iconName: "more"
                        toolTip: qsTr("More options")
                        Accessible.name: qsTr("More options")
                        onClicked: page.openRemoveAll(moreButton)
                    }

                    Item { Layout.fillWidth: true }

                    ViewSwitch {}
                }

                Item { width: 1; height: 20; visible: page.songTab && App.offline.songs.count > 0 }

                // A collection tab with nothing to show (.empty-state).
                CompactEmptyState {
                    width: parent.width
                    visible: App.offline.emptyTabText !== ""
                    compact: false
                    text: App.offline.emptyTabText
                }

                Item { width: 1; height: 18; visible: App.offline.emptyTabText !== "" }

                // .offline-empty: padding 64px 24px, 8px apart; the icon in a
                // 72px accent-tinted circle (8px more below it), the title
                // 16px 600, the sentence 13px in the tertiary colour.
                Column {
                    width: parent.width
                    visible: !App.offline.hasDownloads && !App.offline.pendingShown
                    topPadding: 64
                    bottomPadding: 64
                    spacing: 8

                    Item {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 72
                        height: 72 + 8

                        Rectangle {
                            width: 72
                            height: 72
                            radius: 36
                            color: Theme.accentSoft

                            Icon {
                                anchors.centerIn: parent
                                name: "download"
                                size: 34
                                color: Theme.accentColor
                            }
                        }
                    }

                    Text {
                        width: parent.width - 48
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("No songs are downloaded on this PC")
                        font: Theme.emptyTitle
                        color: Theme.fg
                    }

                    Text {
                        width: parent.width - 48
                        anchors.horizontalCenter: parent.horizontalCenter
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        text: qsTr("Use Download on any song, album or playlist, and it will be here, even with no internet.")
                        font: Theme.viewSubtitle
                        color: Theme.fgTertiary
                    }
                }
            }

            Item { width: 1; height: 4 }
        }
    }

    // openRemoveAllMenu: "Remove all downloads", then, since that is hard
    // to undo on a slow line, one more item that says how many songs go.
    property point removeAllAt
    property bool removeAllConfirming: false

    function openRemoveAll(button: Item) {
        const place = button.mapToItem(page, 0, button.height + 4)
        page.removeAllAt = Qt.point(place.x, place.y)
        page.removeAllConfirming = false
        removeItems.clear()
        removeItems.addItem("remove-all", qsTr("Remove all downloads"), "trash", true)
        removeMenu.openAt(place.x, place.y)
    }

    function confirmRemoveAll() {
        const count = App.offline.downloadedCount
        page.removeAllConfirming = true
        removeItems.clear()
        removeItems.addItem("confirm", count === 1 ? qsTr("Remove 1 song from this PC")
                                                   : qsTr("Remove all %1 songs from this PC").arg(count), "trash", true)
        removeMenu.openAt(page.removeAllAt.x, page.removeAllAt.y)
    }

    // .offline-pending: one bar above the downloaded songs (how many, the
    // song coming in now, how many are done, the speed and the batch's
    // bar), opening to every song on its way with a cancel button each.
    // A long queue never pushes the downloaded songs down.
    component PendingStrip: Item {
        id: strip

        property bool open: false
        readonly property bool active: App.offline.pendingActive
        readonly property bool onlyFailed: !active && App.offline.pendingFailed > 0

        implicitHeight: box.height

        BoxShadow {
            anchors.fill: box
            radius: 12
            layers: Theme.shadow
        }

        Rectangle {
            id: box

            width: parent.width
            height: stripColumn.implicitHeight
            radius: 12
            color: Theme.surface
            border.width: 1
            border.color: Theme.stroke
            clip: true

            Column {
                id: stripColumn

                width: parent.width

                // .offline-pending-bar: 14px apart, padded 12px 16px.
                RowLayout {
                    x: 16
                    width: parent.width - 2 * 16
                    height: Math.max(implicitHeight, 0) + 2 * 12
                    spacing: 14

                    Item {
                        implicitWidth: 26
                        implicitHeight: 26

                        DownloadRing {
                            anchors.centerIn: parent
                            visible: !strip.onlyFailed
                            size: 26
                            percent: App.offline.pendingPercent
                        }

                        Icon {
                            anchors.centerIn: parent
                            visible: strip.onlyFailed
                            name: "alert"
                            size: 22
                            color: Theme.danger
                        }
                    }

                    // .offline-pending-summary: the title 14px 600 with the
                    // speed 10px after it; the line 12px tertiary; the bar
                    // 6px below, at most 420px.
                    Column {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2

                        Row {
                            width: parent.width
                            spacing: 10

                            Text {
                                width: Math.min(implicitWidth, parent.width - (speed.visible ? speed.implicitWidth + 10 : 0))
                                text: App.offline.pendingTitle
                                font: Theme.stripTitle
                                color: Theme.fg
                                elide: Text.ElideRight
                            }

                            Text {
                                id: speed

                                anchors.baseline: parent.children[0].baseline
                                visible: App.offline.pendingSpeed !== ""
                                text: App.offline.pendingSpeed
                                font: Theme.stripSpeed
                                color: Theme.accentColor
                            }
                        }

                        Text {
                            width: parent.width
                            text: App.offline.pendingDetail
                            font: Theme.stripMeta
                            color: Theme.fgTertiary
                            elide: Text.ElideRight
                        }

                        Item {
                            width: 1
                            height: 4
                            visible: strip.active
                        }

                        ProgressLine {
                            visible: strip.active
                            width: Math.min(420, parent.width)
                            percent: App.offline.pendingPercent
                            // A paused batch's bar is greyed.
                            fillColor: App.offline.paused ? Theme.fgSecondary : Theme.accentColor
                        }
                    }

                    StripPill {
                        visible: App.offline.pendingFailed > 0
                        text: App.offline.pendingFailed > 1 ? qsTr("Retry all") : qsTr("Retry")
                        onClicked: App.offline.retryAll()
                    }

                    StripPill {
                        visible: strip.active && App.offline.paused
                        text: qsTr("Resume")
                        iconName: "play"
                        Accessible.name: qsTr("Resume downloads")
                        onClicked: App.offline.resumeAll()
                    }

                    StripRound {
                        visible: strip.active && !App.offline.paused
                        iconName: "pause"
                        iconSize: 12
                        toolTipText: qsTr("Pause downloads")
                        onClicked: App.offline.pauseAll()
                    }

                    // .offline-pending-toggle: 12.5px 600 accent, a chevron
                    // turned down (or up when open) 4px after.
                    LinkButton {
                        id: toggle

                        text: strip.open ? qsTr("Hide") : qsTr("Show all")
                        labelFont: Theme.stripToggle
                        Accessible.name: text
                        implicitWidth: toggleRow.implicitWidth
                        contentItem: Row {
                            id: toggleRow

                            spacing: 4

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: toggle.text
                                font: Theme.stripToggle
                                color: Theme.accentColor
                            }

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron"
                                size: 12
                                color: Theme.accentColor
                                rotation: strip.open ? 270 : 90
                            }
                        }
                        onClicked: strip.open = !strip.open
                    }

                    StripRound {
                        iconName: "close"
                        iconSize: 14
                        toolTipText: strip.active ? qsTr("Cancel all downloads") : qsTr("Remove failed downloads")
                        onClicked: App.offline.cancelAll()
                    }
                }

                // .offline-pending-list: a hairline above, padded 6px 0, at
                // most 420px before it scrolls.
                Rectangle {
                    visible: strip.open
                    width: parent.width
                    height: 1
                    color: Theme.stroke
                }

                ListView {
                    id: list

                    visible: strip.open
                    width: parent.width
                    height: visible ? Math.min(420, contentHeight + 2 * 6) : 0
                    topMargin: 6
                    bottomMargin: 6
                    clip: true
                    interactive: contentHeight + 12 > height
                    boundsBehavior: Flickable.StopAtBounds
                    model: App.offline.pending

                    AppScrollBar.vertical: AppScrollBar {}

                    delegate: PendingRow {
                        width: list.width
                    }
                }
            }
        }
    }

    // .offline-pending-row: 36px cover, the title and artist, the status
    // (120 to 200px), Retry when failed, and cancel; 14px apart, padded
    // 7px 16px.
    component PendingRow: Item {
        id: row

        required property var modelData
        readonly property bool failed: modelData.state === "failed"

        implicitHeight: 36 + 2 * 7

        RowLayout {
            x: 16
            width: parent.width - 2 * 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            ArtTile {
                Layout.preferredWidth: 36
                Layout.preferredHeight: 36
                radius: 6
                source: row.modelData.artworkUrl
                artworkId: "pending:" + row.modelData.itemId
                label: ""
            }

            Column {
                Layout.fillWidth: true
                Layout.minimumWidth: 0

                Text {
                    width: parent.width
                    text: row.modelData.title
                    font: Theme.stripTitle
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    text: row.modelData.artist
                    font: Theme.stripMeta
                    color: Theme.fgTertiary
                    elide: Text.ElideRight
                }
            }

            // .offline-pending-status: a bar and the percent (or what has
            // arrived), else "Starting...", "Paused", "Queued" or "Failed".
            RowLayout {
                Layout.minimumWidth: 120
                Layout.maximumWidth: 200
                Layout.preferredWidth: 200
                spacing: 10

                ProgressLine {
                    Layout.fillWidth: true
                    visible: row.modelData.state === "progress" || row.modelData.state === "receiving"
                    percent: row.modelData.percent
                    indeterminate: row.modelData.state === "receiving"
                    Accessible.name: qsTr("Downloading %1").arg(row.modelData.title)
                }

                Text {
                    visible: row.modelData.state === "progress" || row.modelData.state === "receiving"
                    Layout.minimumWidth: 32
                    horizontalAlignment: Text.AlignRight
                    text: row.modelData.state === "progress" ? row.modelData.percent + "%" : row.modelData.received
                    font: Theme.stripMetaNumbers
                    color: Theme.accentColor
                }

                Text {
                    Layout.fillWidth: true
                    visible: row.modelData.state !== "progress" && row.modelData.state !== "receiving"
                    text: row.failed ? qsTr("Failed")
                        : row.modelData.state === "starting" ? qsTr("Starting\u2026")
                        : row.modelData.state === "paused" ? qsTr("Paused") : qsTr("Queued")
                    font: Theme.stripMeta
                    color: row.failed ? Theme.danger : Theme.fgTertiary
                    elide: Text.ElideRight

                    HoverHandler {
                        id: errorHover

                        enabled: row.failed && row.modelData.error !== ""
                        cursorShape: Qt.WhatsThisCursor
                    }

                    ToolTip.visible: errorHover.hovered
                    ToolTip.text: row.modelData.error
                    ToolTip.delay: 500
                }
            }

            StripPill {
                visible: row.failed
                text: qsTr("Retry")
                onClicked: App.offline.retry(row.modelData.jobId, row.modelData.itemId)
            }

            StripRound {
                iconName: "close"
                iconSize: 14
                toolTipText: row.failed ? qsTr("Remove") : qsTr("Cancel download")
                Accessible.name: (row.failed ? qsTr("Remove %1") : qsTr("Cancel download %1")).arg(row.modelData.title)
                onClicked: App.offline.cancel(row.modelData.jobId, row.modelData.itemId)
            }
        }
    }

    // .offline-progress: a 4px bar on the strong stroke, the fill in the
    // accent; with the size unknown a 35% stretch sweeps across (1.4s), or,
    // under reduced motion, a full faint bar.
    component ProgressLine: Rectangle {
        id: line

        property int percent: 0
        property bool indeterminate: false
        property color fillColor: Theme.accentColor

        implicitHeight: 4
        height: 4
        radius: 2
        color: Theme.strokeStrong
        clip: true

        Rectangle {
            id: bar

            height: parent.height
            radius: 2
            color: line.fillColor
            opacity: line.indeterminate && Motion.reduced ? 0.45 : 1
            width: line.indeterminate ? (Motion.reduced ? line.width : line.width * 0.35)
                                      : line.width * Math.max(0, Math.min(100, line.percent)) / 100
            x: 0

            Behavior on width {
                enabled: !Motion.reduced && !line.indeterminate
                NumberAnimation { duration: 240 }
            }

            NumberAnimation on x {
                running: line.indeterminate && !Motion.reduced && line.visible
                from: -line.width * 0.35
                to: line.width
                duration: 1400
                easing.type: Easing.InOutQuad
                loops: Animation.Infinite
                onStopped: bar.x = 0
            }
        }
    }

    // .offline-retry: 28px, padded 0 12px, an accent border and text, radius
    // 9px, 12px 600; an icon 6px before the text for Resume.
    component StripPill: T.Button {
        id: pill

        property string iconName: ""

        implicitHeight: 28
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Row {
            spacing: 6

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: pill.iconName !== ""
                name: pill.iconName
                size: 12
                color: Theme.accentColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: pill.text
                font: Theme.stripAction
                color: Theme.accentColor
            }
        }

        background: ButtonSurface {
            radius: 9
            border.width: 1
            border.color: Theme.accentColor
            color: pill.hovered ? Theme.hover : "transparent"
            focusShown: pill.visualFocus
        }
    }

    // .offline-cancel: a 28px circle with a faint border (the text colour at
    // 12%), the hover colour on hover.
    component StripRound: T.Button {
        id: round

        property string iconName: ""
        property int iconSize: 14
        property string toolTipText: ""

        implicitWidth: 28
        implicitHeight: 28
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: toolTipText

        ToolTip.visible: hovered && toolTipText !== ""
        ToolTip.text: toolTipText
        ToolTip.delay: 500

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: round.iconName
                size: round.iconSize
                color: Theme.fg
            }
        }

        background: ButtonSurface {
            radius: height / 2
            border.width: 1
            border.color: Qt.alpha(Theme.fg, 0.12)
            color: round.hovered ? Theme.hover : "transparent"
            focusShown: round.visualFocus
        }
    }

    // The page scrolls itself; Back returns its list to where it was.
    ScrollMemory {
        target: App.offline.showsCards ? cardsView : songs.view
    }

    TrackListView {
        id: songs

        anchors.fill: parent
        scrollLaneTopMargin: Theme.radius2xl
        visible: !App.offline.showsCards
        model: App.offline.songs
        grid: App.settings.trackView !== "list"
        sidePadding: page.gutter
        rightSidePadding: page.rightGutter
        header: top
        onActivated: row => App.offline.playRow(row)
        onLikeRequested: row => App.offline.likeRow(row)
        onMenuRequested: (row, at, fromKeyboard) => {
            App.offline.prepareSongMenu(row)
            menus.open(songs, at.x, at.y, fromKeyboard)
        }
    }

    GridView {
        id: cardsView

        readonly property int columns: Math.max(2, Math.floor(cardsView.width / 176))

        anchors.fill: parent
        anchors.leftMargin: page.gutter
        anchors.rightMargin: page.rightGutter
        visible: App.offline.showsCards
        clip: true
        header: top
        cellWidth: cardsView.width / cardsView.columns
        cellHeight: cardsView.cellWidth + 56
        model: App.offline.cards

        AppScrollBar.vertical: AppScrollBar {}

        delegate: CollectionCard {
            required property int index

            width: cardsView.cellWidth - 16
            hasMenu: false
            onClicked: App.offline.openCard(index)
        }

        WheelScroll { flickable: cardsView }
    }

    MenuItemListModel {
        id: removeItems
    }

    ActionMenu {
        id: removeMenu

        items: removeItems
        onTriggered: itemId => {
            if (itemId === "remove-all")
                Qt.callLater(page.confirmRemoveAll)
            else if (itemId === "confirm")
                App.offline.removeAll()
        }
    }

    TrackMenus {
        id: menus
    }
}
