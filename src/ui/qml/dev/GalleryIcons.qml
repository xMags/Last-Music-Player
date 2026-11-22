pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Every icon with its name, at the size the sidebar and buttons use.
GallerySection {
    id: root

    title: qsTr("Icons")

    GridView {
        width: root.width
        height: contentHeight
        interactive: false
        cellWidth: 104
        cellHeight: 64
        model: Icons.names

        delegate: Column {
            id: cell

            required property string modelData

            width: 104
            spacing: 6

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: cell.modelData
                size: 24
                color: Theme.fg
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: cell.modelData
                font: Theme.trackArtist
                color: Theme.fgTertiary
                elide: Text.ElideMiddle
            }
        }
    }
}
