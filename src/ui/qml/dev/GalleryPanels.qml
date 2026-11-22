import QtQuick
import LastMusicPlayer

// The content panel's background in the current theme and accent: idle, and
// with the drifting glow it shows while music plays.
GallerySection {
    id: root

    title: qsTr("Content panel")

    Row {
        spacing: 20

        ContentPanel {
            width: (root.width - 20) / 2
            height: 220
        }

        ContentPanel {
            width: (root.width - 20) / 2
            height: 220
            playing: true
        }
    }
}
