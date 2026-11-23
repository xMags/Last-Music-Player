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
    property Component header: null
    // Space below the last row, inside the scrolling area.
    property real bottomMargin: 32
    // Space at each side of the rows; the scroll bar sits in the right one.
    property real sidePadding: 0

    signal activated(int row)
    signal likeRequested(int row)
    // `at` is in this item's coordinates.
    signal menuRequested(int row, point at, bool fromKeyboard)

    readonly property int rowHeight: 36 + 2 * 8 + 1
    readonly property int headHeight: 41
    // The list or grid that scrolls, for keeping its position.
    readonly property Flickable view: form.item as Flickable

    Loader {
        id: form

        anchors.fill: parent
        anchors.leftMargin: root.sidePadding
        anchors.rightMargin: root.sidePadding
        sourceComponent: root.grid ? gridForm : listForm
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
            }

            header: Column {
                width: list.rowWidth

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

                    Row {
                        x: 24
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 16

                        TableHeading { width: 40; text: "#"; horizontalAlignment: Text.AlignHCenter }
                        TableHeading { width: list.rowWidth - 24 - 40 - 16 - (albumHeading.visible ? 216 : 0) - 60 - 76; text: qsTr("TITLE") }
                        TableHeading { id: albumHeading; width: 200; visible: list.rowWidth >= 560; text: qsTr("ALBUM") }
                        TableHeading { width: 44; text: qsTr("TIME"); horizontalAlignment: Text.AlignRight }
                    }
                }
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

                width: list.rowWidth
                onClicked: root.activated(row.index)
                onLikeRequested: root.likeRequested(row.index)
                onMenuRequested: (at, fromKeyboard) => root.menuRequested(row.index, row.mapToItem(root, at), fromKeyboard)
                Keys.onReturnPressed: root.activated(row.index)
                Keys.onEnterPressed: root.activated(row.index)
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
            keyNavigationEnabled: true
            currentIndex: -1

            header: Loader {
                width: cards.width
                sourceComponent: root.header
            }

            delegate: Item {
                id: cell

                required property int index
                required property string title
                required property string artist
                required property url artworkUrl
                required property string artworkId
                required property string artworkLabel
                required property bool playing
                required property bool unavailable
                required property string unavailableReason

                width: cards.cellWidth
                height: cards.cellHeight

                TrackTile {
                    width: cards.cardWidth
                    title: cell.title
                    artist: cell.artist
                    artworkUrl: cell.artworkUrl
                    artworkId: cell.artworkId
                    artworkLabel: cell.artworkLabel
                    playing: cell.playing
                    unavailable: cell.unavailable
                    unavailableReason: cell.unavailableReason
                    onClicked: root.activated(cell.index)
                    onMenuRequested: (at, fromKeyboard) => root.menuRequested(cell.index, mapToItem(root, at), fromKeyboard)
                    Keys.onReturnPressed: root.activated(cell.index)
                }
            }
        }
    }
}
