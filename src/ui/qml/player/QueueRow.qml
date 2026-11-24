pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// One Up Next row (.queue-item, styles.css 1278-1330 and 1960-1975): the
// 44px cover, title and artist, the length and the "..." menu button, padded
// 6px 8px on a surface with the 8px radius that lights while pointed at. A
// lifted row (carried by the pointer) rises with the flyout shadow, and the
// row the lifted one would land before or after shows a 2px accent line on
// that side. Carried rows settle back over Motion.queueRowSettle.
//
// The row only reports input; the list decides what it means. Rows without
// `showMore` (the idle panel's) take left clicks only and have no menu.
Item {
    id: row

    required property int index
    required property string rowKey
    required property string title
    required property string artistLine
    required property url artworkUrl
    required property string artworkId
    required property string artworkLabel
    required property string durationText
    required property string remoteUrl

    property bool showMore: true
    property bool lifted: false
    // -1: the lifted row lands before this one, 1: after it, 0: neither.
    property int dropEdge: 0
    // How far the row is carried from its place.
    property real carried: 0
    property color titleColor: Theme.fg
    property color subColor: Theme.fgTertiary
    property color hoverColor: Theme.hover

    // Left button, in the row's coordinates.
    signal pressedAt(point at)
    signal movedTo(point at)
    signal released()
    signal canceled()
    signal clicked()
    // Right click, the "..." button or the keyboard (Menu key, Shift+F10).
    signal menuRequested(point at, bool fromKeyboard)
    // Enter or Space.
    signal activated()

    // padding 6px top and bottom around the 44px cover.
    implicitHeight: 44 + 2 * 6
    activeFocusOnTab: true
    z: lifted ? 3 : 0
    Accessible.role: Accessible.Button
    Accessible.name: title

    transform: Translate {
        y: row.carried

        Behavior on y {
            enabled: !row.lifted && !Motion.reduced
            NumberAnimation { duration: Motion.queueRowSettle; easing: Motion.ease }
        }
    }

    Keys.onMenuPressed: row.menuRequested(Qt.point(12, row.height - 8), true)
    Keys.onReturnPressed: row.activated()
    Keys.onEnterPressed: row.activated()
    Keys.onSpacePressed: row.activated()

    Shortcut {
        sequence: "Shift+F10"
        enabled: row.activeFocus && row.showMore
        onActivated: row.menuRequested(Qt.point(12, row.height - 8), true)
    }

    HoverHandler {
        id: hover
    }

    BoxShadow {
        anchors.fill: parent
        visible: row.lifted
        layers: Theme.shadowFlyout
        radius: Theme.radiusMd
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: row.lifted ? Theme.surfaceAlt : hover.hovered ? row.hoverColor : "transparent"
    }

    // box-shadow: inset 0 2px 0 accent (before) or inset 0 -2px 0 (after).
    Rectangle {
        y: row.dropEdge < 0 ? 0 : row.height - height
        width: row.width
        height: 2
        visible: row.dropEdge !== 0
        color: Theme.accentColor
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: row.showMore ? Qt.LeftButton | Qt.RightButton : Qt.LeftButton
        preventStealing: true
        cursorShape: row.lifted ? Qt.ClosedHandCursor : Qt.ArrowCursor
        onPressed: event => event.button === Qt.RightButton ? row.menuRequested(Qt.point(event.x, event.y), false) : row.pressedAt(Qt.point(event.x, event.y))
        onPositionChanged: event => row.movedTo(Qt.point(event.x, event.y))
        onReleased: row.released()
        onCanceled: row.canceled()
        onClicked: event => {
            if (event.button === Qt.LeftButton)
                row.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 12

        ArtTile {
            Layout.preferredWidth: 44
            Layout.preferredHeight: 44
            radius: Theme.radiusMd
            source: row.artworkUrl
            artworkId: row.artworkId
            label: row.artworkLabel
        }

        Column {
            Layout.fillWidth: true
            Layout.minimumWidth: 0

            // .queue-title-line: the title, its download mark 6px after.
            Row {
                width: parent.width
                spacing: 6

                Text {
                    width: Math.min(implicitWidth, parent.width - (queueMark.visible ? queueMark.width + 6 : 0))
                    text: row.title
                    font: Theme.trackTitle
                    lineHeight: Theme.lineHeightTrackTitle
                    lineHeightMode: Text.FixedHeight
                    color: row.titleColor
                    elide: Text.ElideRight
                }

                DownloadMark {
                    id: queueMark

                    anchors.verticalCenter: parent.verticalCenter
                    remoteUrl: row.remoteUrl
                }
            }

            Text {
                width: parent.width
                text: row.artistLine
                font: Theme.trackArtist
                lineHeight: Theme.lineHeightTrackArtist
                lineHeightMode: Text.FixedHeight
                color: row.subColor
                elide: Text.ElideRight
            }
        }

        Text {
            text: row.durationText
            font: Theme.queueDuration
            color: row.subColor
        }

        // .queue-more: a 28px round button, margin 0 -4px 0 -6px.
        T.AbstractButton {
            id: more

            Layout.preferredWidth: 28
            Layout.preferredHeight: 28
            Layout.leftMargin: -6
            Layout.rightMargin: -4
            visible: row.showMore
            padding: 0
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("More options for %1").arg(row.title)
            onClicked: row.menuRequested(more.mapToItem(row, more.width, more.height), more.visualFocus)

            background: Rectangle {
                radius: more.width / 2
                color: more.hovered ? row.hoverColor : "transparent"

                FocusRing {
                    shown: more.visualFocus
                    controlRadius: more.width / 2
                    offset: -2
                }
            }

            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: "more"
                    size: 18
                    color: more.hovered ? row.titleColor : row.subColor
                }
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    // :focus-visible: a 2px accent outline inside the row.
    FocusRing {
        shown: row.activeFocus
        controlRadius: Theme.radiusMd
        offset: -2
    }
}
