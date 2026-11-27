import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A Browse category (.search-category-card, views.css 1855-1900): a 104px
// tile of the category's colour with its name (two lines at most) and, in
// the lower right, a square tilted 22 degrees: one of the user's own covers
// with a shadow, or a darker shape.
T.AbstractButton {
    id: tile

    required property string kind
    required property string title
    required property url artworkUrl
    required property color gradientStart

    implicitHeight: 104
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Browse %1").arg(title)

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: 16
        color: tile.gradientStart

        // The tilted cover runs past the corner: cut to the tile's corners.
        RoundedClip {
            anchors.fill: parent
            radius: 16

            Item {
                x: parent.width - 72 + 8
                y: parent.height - 72 + 10
                width: 72
                height: 72
                rotation: 22

                BoxShadow {
                    anchors.fill: parent
                    visible: tile.artworkUrl.toString() !== ""
                    // i.has-cover: 0 6px 18px rgba(0, 0, 0, .28).
                    layers: Theme.shadowCategoryCover
                    radius: 12
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 12
                    visible: tile.artworkUrl.toString() === ""
                    color: Qt.rgba(0, 0, 0, 0.18)
                }

                ArtTile {
                    anchors.fill: parent
                    visible: tile.artworkUrl.toString() !== ""
                    radius: 12
                    source: tile.artworkUrl
                    artworkId: "category:" + tile.title
                    bare: true
                }
            }
        }

        FocusRing {
            shown: tile.visualFocus
        }
    }

    contentItem: Item {
        // A <button> centres its content: the name sits in the middle of
        // the tile's height, inside its 16px padding.
        AppText {
            x: 16
            y: Math.max(16, (parent.height - height) / 2)
            width: parent.width - 32 - 36
            text: tile.title
            font: Theme.searchCategory
            lineHeight: Theme.lineHeightSearchCategory
            lineHeightMode: Text.FixedHeight
            color: "white"
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }
    }

    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: tile.contentItem
    }
}
