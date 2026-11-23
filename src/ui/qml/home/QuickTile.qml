import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A shortcut on Home (.home-quick-tile, styles.css 2511-2600): 64px high,
// the cover (or a system playlist's gradient and icon) at the left, the name
// beside it, tinted by its cover, with a wash fading it out towards the right.
// A click plays the collection; right click and the Menu key open its menu.
T.AbstractButton {
    id: tile

    required property string kind
    required property string title
    required property url artworkUrl
    required property string artworkId
    required property string artworkLabel
    required property color gradientStart
    required property color gradientEnd
    required property string iconName

    signal menuRequested(point at)

    implicitHeight: 64
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Play %1").arg(title)

    Keys.onMenuPressed: tile.menuRequested(Qt.point(24, height / 2))

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => tile.menuRequested(eventPoint.position)
    }

    background: Rectangle {
        radius: 8
        clip: true
        color: tile.hovered ? Qt.alpha(Theme.surface, 0.30) : Qt.alpha(Theme.surface, 0.55)
        border.width: 1
        border.color: Theme.stroke

        // The tint: the cover's colours, very soft.
        Item {
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            opacity: Theme.dark ? 0.5 : 0.42

            Image {
                anchors.fill: parent
                visible: tile.kind !== "system"
                source: tile.artworkUrl
                sourceSize.width: 8
                sourceSize.height: 8
                fillMode: Image.Stretch
                smooth: true
                asynchronous: true
            }

            Rectangle {
                anchors.fill: parent
                visible: tile.kind === "system"
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: tile.gradientStart }
                    GradientStop { position: 1; color: tile.gradientEnd }
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 8
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 1; color: Qt.alpha(Theme.bg, 0.55) }
            }
        }

        FocusRing {
            shown: tile.visualFocus
        }
    }

    contentItem: Item {
        Item {
            id: art

            width: 64
            height: parent.height

            Rectangle {
                anchors.fill: parent
                visible: tile.kind === "system"
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: tile.gradientStart }
                    GradientStop { position: 1; color: tile.gradientEnd }
                }

                Icon {
                    anchors.centerIn: parent
                    name: tile.iconName
                    size: 24
                    color: "white"
                }
            }

            ArtTile {
                anchors.fill: parent
                visible: tile.kind !== "system"
                radius: 0
                source: tile.artworkUrl
                artworkId: tile.artworkId
                label: tile.artworkLabel
            }
        }

        Text {
            x: art.width + 14
            width: parent.width - x - 14
            anchors.verticalCenter: parent.verticalCenter
            text: tile.title
            font: Theme.quickTitle
            color: Theme.fg
            elide: Text.ElideRight
        }
    }

    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: tile.contentItem
    }
}
