pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
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

                // Downloads on their way (Songs only).
                Rectangle {
                    width: parent.width
                    visible: App.offline.pendingShown && App.offline.tab === "songs"
                    implicitHeight: pendingColumn.implicitHeight + 24
                    radius: 12
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.stroke

                    Column {
                        id: pendingColumn

                        x: 16
                        y: 12
                        width: parent.width - 32
                        spacing: 10

                        RowLayout {
                            width: pendingColumn.width
                            spacing: 10

                            Column {
                                Layout.fillWidth: true
                                spacing: 2

                                Text {
                                    text: App.offline.pendingTitle
                                    font: Theme.collectionInfoStrong
                                    color: Theme.fg
                                }

                                Text {
                                    width: parent.width
                                    text: App.offline.pendingDetail
                                    font: Theme.collectionInfo
                                    color: Theme.fgSecondary
                                    elide: Text.ElideRight
                                }
                            }

                            AppButton {
                                visible: App.offline.pendingActive
                                text: App.offline.paused ? qsTr("Resume") : qsTr("Pause")
                                iconName: App.offline.paused ? "play" : "pause"
                                iconSize: 12
                                onClicked: App.offline.paused ? App.offline.resumeAll() : App.offline.pauseAll()
                            }
                        }

                        Rectangle {
                            width: pendingColumn.width
                            height: 4
                            radius: 2
                            color: Theme.sliderTrack

                            Rectangle {
                                width: parent.width * App.offline.pendingPercent / 100
                                height: parent.height
                                radius: 2
                                color: Theme.accentColor
                            }
                        }

                        Repeater {
                            model: App.offline.pending

                            RowLayout {
                                id: pendingRow

                                required property var modelData

                                width: pendingColumn.width
                                spacing: 10

                                Column {
                                    Layout.fillWidth: true

                                    Text {
                                        width: parent.width
                                        text: pendingRow.modelData.title
                                        font: Theme.body
                                        color: Theme.fg
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        width: parent.width
                                        text: pendingRow.modelData.failed && pendingRow.modelData.error !== ""
                                            ? pendingRow.modelData.error
                                            : pendingRow.modelData.state
                                                + (pendingRow.modelData.percent > 0 ? " · " + pendingRow.modelData.percent + "%" : "")
                                        font: Theme.collectionInfo
                                        color: pendingRow.modelData.failed ? Theme.danger : Theme.fgSecondary
                                        elide: Text.ElideRight
                                    }
                                }

                                AppButton {
                                    visible: pendingRow.modelData.failed
                                    text: qsTr("Retry")
                                    onClicked: App.offline.retry(pendingRow.modelData.id)
                                }

                                IconButton {
                                    iconName: "close"
                                    Accessible.name: pendingRow.modelData.failed
                                        ? qsTr("Remove %1").arg(pendingRow.modelData.title)
                                        : qsTr("Cancel download %1").arg(pendingRow.modelData.title)
                                    onClicked: App.offline.cancel(pendingRow.modelData.id)
                                }
                            }
                        }
                    }
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

    TrackListView {
        id: songs

        anchors.fill: parent
        visible: !App.offline.showsCards
        model: App.offline.songs
        grid: App.settings.trackView !== "list"
        sidePadding: page.gutter
        rightSidePadding: page.rightGutter
        header: top
        onActivated: row => App.offline.playRow(row)
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
