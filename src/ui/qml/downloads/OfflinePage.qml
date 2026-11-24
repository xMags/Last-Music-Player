pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The Offline page (App.offline; the reference's views/offline.ts): the
// songs downloaded on this PC, with Albums and Artists cut down to what is
// downloaded; above them the strip of downloads on their way, with pause or
// resume, cancel and retry; the storage summary and Clear completed. While
// the connection is down a notice says downloaded songs still play.
Item {
    id: page

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane

    function groupLabel(): string {
        if (App.offline.groupTitle === "")
            return qsTr("DOWNLOADS")
        return App.offline.tab === "albums" ? qsTr("ALBUM") : qsTr("ARTIST")
    }

    Component {
        id: top

        Column {
            id: head

            readonly property real inner: width - (page.gutter + page.rightGutter)

            width: songs.width
            leftPadding: page.gutter
            rightPadding: page.rightGutter
            topPadding: 32
            spacing: 16

            RowLayout {
                width: head.inner
                spacing: 12

                Column {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: page.groupLabel()
                        font: Theme.eyebrow
                        color: Theme.fgSecondary
                    }

                    Text {
                        width: parent.width
                        text: App.offline.groupTitle !== "" ? App.offline.groupTitle : qsTr("Offline")
                        font: Theme.settingsHeading
                        color: Theme.fg
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: App.offline.summary !== ""
                        text: App.offline.summary
                        font: Theme.body
                        color: Theme.fgSecondary
                    }
                }

                AppButton {
                    visible: App.offline.groupTitle !== ""
                    text: qsTr("Back")
                    onClicked: App.offline.closeGroup()
                }

                AppButton {
                    visible: App.offline.hasCompleted && App.offline.groupTitle === ""
                    text: qsTr("Clear completed")
                    onClicked: App.offline.clearCompleted()
                }
            }

            // You're offline.
            Rectangle {
                width: head.inner
                height: 40
                radius: 10
                visible: App.connectivity.offline
                color: Theme.accentSoft

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    spacing: 8

                    Icon {
                        name: "wifi-off"
                        size: 16
                    }

                    Text {
                        text: qsTr("You're offline. Downloaded songs still play.")
                        font: Theme.body
                        color: Theme.fg
                    }
                }
            }

            // Downloads on their way.
            Rectangle {
                width: head.inner
                visible: App.offline.pendingShown
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

            SegmentedControl {
                visible: App.offline.hasDownloads && App.offline.groupTitle === ""
                choices: App.offline.tabs
                currentIndex: App.offline.tab === "albums" ? 1 : App.offline.tab === "artists" ? 2 : 0
                onActivated: index => App.offline.tab = index === 1 ? "albums" : index === 2 ? "artists" : "songs"
            }

            Row {
                visible: !App.offline.showsCards && App.offline.songs.count > 0
                spacing: 10

                PlayPill {
                    onClicked: App.offline.play()
                }

                RoundAction {
                    iconName: "shuffle"
                    Accessible.name: qsTr("Shuffle")
                    onClicked: App.offline.shuffle()
                }
            }

            // Nothing downloaded yet.
            Column {
                width: head.inner
                visible: !App.offline.hasDownloads && !App.offline.pendingShown
                topPadding: 48
                spacing: 8

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "download"
                    size: 34
                    color: Theme.fgSecondary
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("No songs are downloaded on this PC")
                    font: Theme.collectionInfoStrong
                    color: Theme.fg
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: qsTr("Use Download on any song, album or playlist, and it will be here, even with no internet.")
                    font: Theme.body
                    color: Theme.fgSecondary
                }
            }

            Item { width: 1; height: 4 }
        }
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
            onClicked: App.offline.openCard(index)
        }

        WheelScroll { flickable: cardsView }
    }

    TrackMenus {
        id: menus
    }
}
