pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A shelf's cards before they are built: as many skeleton cards as the row
// shows, each the cover block with its glyph and, when the card has text
// under its cover, a title and a subtitle line, under one sweep. The lines
// sit inside the card's own text area, so nothing moves when the real cards
// replace them.
Item {
    id: root

    required property int cardWidth
    required property int cardHeight
    required property int cardSpacing
    // Round covers (artist cards) get round blocks and a person glyph.
    property bool round: false

    clip: true

    Row {
        spacing: root.cardSpacing

        Repeater {
            model: Math.ceil((root.width + root.cardSpacing) / (root.cardWidth + root.cardSpacing))

            Item {
                width: root.cardWidth
                height: root.cardHeight

                Skeleton {
                    width: root.cardWidth
                    height: root.cardWidth
                    radius: root.round ? root.cardWidth / 2 : Theme.radiusXl
                    glyph: root.round ? "person" : "note"
                }

                // Title 14 px under the cover, the subtitle 9 px under it.
                Skeleton {
                    visible: root.cardHeight > root.cardWidth
                    y: root.cardWidth + 14
                    width: root.cardWidth * 0.76
                    height: 12
                    radius: 6
                    strong: true
                }

                Skeleton {
                    visible: root.cardHeight > root.cardWidth
                    y: root.cardWidth + 14 + 12 + 9
                    width: root.cardWidth * 0.52
                    height: 10
                    radius: 5
                }
            }
        }
    }

    SkeletonSweep {
        anchors.fill: parent
    }
}
