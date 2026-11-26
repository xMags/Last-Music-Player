pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A shelf's cards before they are built: as many skeleton cards as the row
// shows, each the cover block and, when the card has text under its cover,
// two lines (as SkeletonGrid draws them), under one sweep.
Item {
    id: root

    required property int cardWidth
    required property int cardHeight
    required property int cardSpacing

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
                    radius: Theme.radiusXl
                }

                // The title and subtitle lines: 12px under the cover.
                Skeleton {
                    visible: root.cardHeight > root.cardWidth
                    y: root.cardWidth + 12 + 4
                    width: root.cardWidth * 0.76
                    height: 14
                }

                Skeleton {
                    visible: root.cardHeight > root.cardWidth
                    y: root.cardWidth + 12 + 22 + 3
                    width: root.cardWidth * 0.52
                    height: 11
                }
            }
        }
    }

    SkeletonSweep {
        anchors.fill: parent
    }
}
