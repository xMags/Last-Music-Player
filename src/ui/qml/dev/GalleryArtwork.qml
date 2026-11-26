import QtQuick
import LastMusicPlayer

// Cover tiles with and without a picture, and the loading skeletons.
Column {
    id: root

    // The gallery's view model, for the sample chart cards.
    property var model: null

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
                radius: Theme.radiusXl
                shadow: true
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Midnight Drive"
                radius: Theme.radiusXl
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Ocean Avenue"
                radius: Theme.radiusXl
            }

            ArtTile {
                width: 160
                height: 160
                artworkId: "Round cover"
                radius: width / 2
            }

            ArtTile {
                width: 240
                height: 240
                artworkId: "Full size"
                big: true
                radius: Theme.radius2xl
                shadow: true
            }
        }
    }

    GallerySection {
        width: root.width
        title: qsTr("Chart cards")

        Flow {
            width: parent.width
            spacing: 20

            Repeater {
                model: root.model ? root.model.sampleCharts : null

                ChartCard {
                    required artSource
                    required artworkUrl
                    required name
                    required family
                    required kindLabel
                    required kindColor
                    required cardSubtitle
                }
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
