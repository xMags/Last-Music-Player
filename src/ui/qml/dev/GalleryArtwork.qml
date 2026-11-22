import QtQuick
import LastMusicPlayer

// Cover tiles with and without a picture, and the loading skeletons.
Column {
    id: root

    spacing: 32

    GallerySection {
        width: root.width
        title: qsTr("Artwork")

        Row {
            spacing: 20

            ArtTile {
                width: 160
                height: 160
                source: "qrc:/qt/qml/LastMusicPlayer/dev/sample-cover.png"
                artworkId: "sample-cover"
                label: "L"
                radius: Theme.radiusXl
                shadow: true
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Midnight Drive"
                label: "M"
                radius: Theme.radiusXl
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Ocean Avenue"
                label: "O"
                radius: Theme.radiusXl
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Round cover"
                label: "R"
                radius: width / 2
            }

            ArtTile {
                width: 240
                height: 240
                artworkId: "Full size"
                label: "F"
                big: true
                radius: Theme.radius2xl
                shadow: true
            }
        }
    }

    GallerySection {
        width: root.width
        title: qsTr("Loading skeletons")

        SkeletonRows {
            width: parent.width
            rows: 3
        }

        SkeletonGrid {
            width: parent.width
            count: 6
        }
    }
}
