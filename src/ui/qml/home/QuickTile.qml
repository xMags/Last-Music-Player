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
        color: tile.hovered ? Qt.alpha(Theme.surface, 0.30) : Qt.alpha(Theme.surface, 0.55)
        border.width: 1
        border.color: Theme.stroke

        // The tint: the cover's colours, very soft, inside the 1px border.
        RoundedClip {
            anchors.fill: parent
            anchors.margins: 1
            radius: 8 - 1
            opacity: Theme.dark ? 0.5 : 0.42

            // inset: -30px; filter: blur(30px) saturate(1.5). The wash is
            // sized so its 1.2px blur stretches to 30px.
            CoverWash {
                anchors.fill: parent
                anchors.margins: -30
                visible: tile.kind !== "system"
                source: tile.artworkUrl
                washSize: Qt.size(Math.max(2, Math.round(width * 1.2 / 30)), Math.max(2, Math.round(height * 1.2 / 30)))
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

        QuickTileField {
            anchors.right: parent.right
            width: parent.width * 0.55
            height: parent.height
        }

        FocusRing {
            shown: tile.visualFocus
        }
    }

    contentItem: Item {
        // The cover square runs to the tile's left edge: cut to its corners.
        // The title stays outside, as text in a clipping layer blurs.
        RoundedClip {
            anchors.fill: parent
            radius: 8

            Item {
                id: art

                width: 64
                height: parent.height

                // 135deg, and the playlist icon at 0.44 of its card size
                // (52px for the heart, 56px otherwise).
                DiagonalGradient {
                    anchors.fill: parent
                    visible: tile.kind === "system"
                    startColor: tile.gradientStart
                    endColor: tile.gradientEnd

                    Icon {
                        anchors.centerIn: parent
                        name: tile.iconName
                        size: Math.round((tile.iconName === "heart-fill" ? 52 : 56) * 0.44)
                        color: "white"
                    }
                }

                ArtTile {
                    anchors.fill: parent
                    visible: tile.kind !== "system"
                    radius: 0
                    source: tile.artworkUrl
                    artworkId: tile.artworkId
                }
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
