pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Placeholder cards while a grid loads (styles.css 746-750; the grid is
// .catalog-grid, views.css 1027-1031): as many 150px-or-wider columns as fit,
// 20px apart, rows 28px apart. Each card is a square cover and two lines,
// under one sweep. `round` draws round covers, as artist cards have.
Item {
    id: root

    property int count: 8
    property bool round: false

    implicitWidth: 760
    implicitHeight: grid.implicitHeight

    GridLayout {
        id: grid

        width: root.width
        columns: Math.max(1, Math.floor((root.width + 20) / (150 + 20)))
        columnSpacing: 20
        rowSpacing: 28

        Repeater {
            model: root.count

            Column {
                id: card

                Layout.fillWidth: true
                Layout.preferredWidth: 150
                Layout.alignment: Qt.AlignTop
                bottomPadding: 8

                Skeleton {
                    width: card.width
                    height: card.width
                    radius: root.round ? width / 2 : Theme.radiusXl
                    glyph: root.round ? "person" : "note"
                }
                // The title 14 px under the cover, the subtitle 9 px under it.
                Item { width: 1; height: 14 }
                Skeleton { width: card.width * 0.76; height: 12; radius: 6; strong: true }
                Item { width: 1; height: 9 }
                Skeleton { width: card.width * 0.52; height: 10; radius: 5 }
            }
        }
    }

    SkeletonSweep {
        anchors.fill: parent
    }

    // Content replacing the skeleton cascades in, as a page opening does.
    onVisibleChanged: {
        if (!visible)
            App.cascade.restart()
    }
    Component.onDestruction: App.cascade.restart()
}
