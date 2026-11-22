pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// A playlist in the sidebar (.playlist-item, styles.css 346-367): a 32px
// swatch beside the name, 14px apart. Liked Songs has the accent gradient
// swatch with a heart; other playlists a plain swatch with a note until
// their covers arrive.
NavItem {
    id: row

    property bool liked: false

    gap: 14
    leading: Item {
        implicitWidth: 32
        implicitHeight: 32

        // linear-gradient(135deg, accent-fill-2, accent-fill) for Liked Songs.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: Theme.neutral
            visible: !row.liked
        }

        Shape {
            anchors.fill: parent
            visible: row.liked
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0
                    y1: 0
                    x2: 32
                    y2: 32
                    GradientStop { position: 0; color: Theme.accentFill2 }
                    GradientStop { position: 1; color: Theme.accentFill }
                }
                PathRectangle { width: 32; height: 32; radius: Theme.radiusSm }
            }
        }

        Icon {
            anchors.centerIn: parent
            name: row.liked ? "heart-fill" : "music-note"
            size: 12
            color: row.liked ? Theme.onOverlay : Theme.fgTertiary
        }
    }
}
