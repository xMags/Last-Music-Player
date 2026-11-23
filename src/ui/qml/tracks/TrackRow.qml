import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// One song in the list form (.track-row, views.css 173-251; common.ts
// trackRow): the number (a play glyph on hover, the bars while it plays),
// the 36px cover with title and artist, the album, the length, the heart and
// the "..." button. The playing row is tinted and its title takes the
// accent; an unavailable row is faded, says why, and does not play.
// Right click, the "..." button, the Menu key and Shift+F10 ask for the menu.
// A collection page adds a compact density (46 rather than 58px in the
// current app; here 41 rather than 53, the reference row less 12) and a
// selected look; `pressModifiers` says which of Ctrl and Shift were held
// when the click began.
T.AbstractButton {
    id: row

    required property int number
    required property string title
    required property string artist
    required property string album
    required property string durationText
    required property url artworkUrl
    required property string artworkId
    required property string artworkLabel
    required property bool liked
    required property bool playing
    required property bool unavailable
    required property string unavailableReason
    // The album column shows only when the row is wide enough for it.
    readonly property bool albumShown: width >= 560
    property bool selected: false
    property bool compact: false
    readonly property int artSize: compact ? 28 : 36
    property int pressModifiers: Qt.NoModifier

    signal likeRequested()
    signal menuRequested(point at, bool fromKeyboard)

    implicitHeight: artSize + 2 * (compact ? 6 : 8) + 1
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Play %1 by %2").arg(title).arg(artist)

    Keys.onMenuPressed: row.menuRequested(Qt.point(24, height / 2), true)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            row.menuRequested(Qt.point(24, row.height / 2), true)
            event.accepted = true
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => row.menuRequested(eventPoint.position, false)
    }

    // Sees the press before the button does, only to note the modifiers.
    TapHandler {
        id: leftPress

        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onPressedChanged: {
            if (pressed)
                row.pressModifiers = leftPress.point.modifiers
        }
    }

    HoverHandler {
        cursorShape: row.unavailable ? Qt.ForbiddenCursor : Qt.PointingHandCursor
    }

    background: Rectangle {
        color: row.selected ? Theme.accentSoft2
             : row.playing ? Theme.accentSel
             : (row.hovered && !row.unavailable ? Theme.rowHover : "transparent")

        // border-bottom: 1px solid divider
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.divider
        }

        FocusRing {
            shown: row.visualFocus
            offset: -2 - Theme.focusRingWidth
        }
    }

    contentItem: RowLayout {
        opacity: row.unavailable ? 0.55 : 1
        spacing: 16

        // padding: 8px 20px
        Item { Layout.preferredWidth: 20 - 16 }

        Item {
            Layout.preferredWidth: 40
            Layout.fillHeight: true

            Text {
                anchors.centerIn: parent
                visible: !row.playing && !(row.hovered && !row.unavailable)
                text: row.number
                font: Theme.trackNumber
                color: Theme.fgTertiary
            }

            Icon {
                anchors.centerIn: parent
                visible: row.hovered && !row.playing
                name: row.unavailable ? "alert" : "play"
                size: 12
                color: row.unavailable ? Theme.danger : Theme.accentColor
            }

            EqBars {
                anchors.centerIn: parent
                visible: row.playing
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 12

            ArtTile {
                Layout.preferredWidth: row.artSize
                Layout.preferredHeight: row.artSize
                radius: 6
                source: row.artworkUrl
                artworkId: row.artworkId
                label: row.artworkLabel
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 0

                Text {
                    Layout.fillWidth: true
                    text: row.title
                    font: Theme.trackTitle
                    color: row.playing ? Theme.accentColor : Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    text: row.unavailable ? row.unavailableReason : row.artist
                    font: Theme.trackArtist
                    color: row.unavailable ? Theme.danger : Theme.fgTertiary
                    elide: Text.ElideRight
                }
            }
        }

        Text {
            Layout.preferredWidth: 200
            visible: row.albumShown
            text: row.album
            font: Theme.trackAlbum
            color: Theme.fgSecondary
            elide: Text.ElideRight
        }

        Text {
            Layout.preferredWidth: 44
            horizontalAlignment: Text.AlignRight
            text: row.unavailable ? "-" : row.durationText
            font: Theme.trackNumber
            color: Theme.fgTertiary
        }

        Row {
            spacing: 2

            IconButton {
                iconName: row.liked ? "heart-fill" : "heart"
                active: row.liked
                shape: IconButton.Square
                iconSize: 14
                implicitWidth: 28
                implicitHeight: 28
                Accessible.name: row.liked ? qsTr("Remove from Liked Songs") : qsTr("Add to Liked Songs")
                onClicked: row.likeRequested()
            }

            IconButton {
                id: more

                iconName: "more"
                Accessible.name: qsTr("More options")
                shape: IconButton.Square
                iconSize: 14
                implicitWidth: 28
                implicitHeight: 28
                opacity: row.hovered || row.visualFocus ? 1 : 0
                onClicked: row.menuRequested(more.mapToItem(row, more.width / 2, more.height), false)
            }
        }

        Item { Layout.preferredWidth: 20 - 16 - 2 }
    }
}
