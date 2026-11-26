pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Song lists, cards and shelves with sample songs: the track table and the
// song grid (switched by the List / Grid control), a shelf of cards, an
// artist card, and the empty and error states.
Column {
    id: root

    required property GalleryViewModel model
    property bool gridForm: false

    spacing: 32

    GallerySection {
        width: root.width
        title: qsTr("Song lists")

        SegmentedControl {
            compact: true
            choices: [qsTr("List"), qsTr("Grid")]
            currentIndex: root.gridForm ? 1 : 0
            onActivated: index => root.gridForm = index === 1
        }

        TrackListView {
            width: parent.width
            height: root.gridForm ? 520 : 360
            model: root.model.sampleTracks
            grid: root.gridForm
            bottomMargin: 0
            onActivated: row => root.model.report(qsTr("Play row %1").arg(row + 1))
            onLikeRequested: row => root.model.report(qsTr("Like row %1").arg(row + 1))
            onMenuRequested: (row, at) => root.model.report(qsTr("Menu for row %1").arg(row + 1))
        }
    }

    GallerySection {
        width: root.width
        title: qsTr("Shelf and cards")

        Shelf {
            width: parent.width
            title: qsTr("Recently added")
            linkText: qsTr("See all")
            model: root.model.sampleTracks
            onLinkClicked: root.model.report(qsTr("See all"))

            delegate: Card {
                required title
                required artworkUrl
                required artworkId
                required property string artist

                width: 156
                subtitle: artist
            }
        }

        Row {
            spacing: 20

            Card {
                width: 160
                round: true
                title: qsTr("Night Owls")
                subtitle: qsTr("Artist")
            }

            Card {
                width: 160
                title: qsTr("Favourites")
                subtitle: qsTr("Playlist · 24 songs")
                art: Component {
                    Rectangle {
                        radius: Theme.radiusXl
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#00b8d4" }
                            GradientStop { position: 1; color: "#008da8" }
                        }

                        Icon {
                            anchors.centerIn: parent
                            name: "heart-fill"
                            size: 40
                            color: "white"
                        }
                    }
                }
            }
        }

        RowLayout {
            width: parent.width
            spacing: 24

            EmptyState {
                Layout.fillWidth: true
                title: qsTr("No songs yet")
                text: qsTr("Choose a music folder to fill your library.")
                actionText: qsTr("Choose music folder")
            }

            ErrorState {
                Layout.fillWidth: true
                title: qsTr("Could not load")
                text: qsTr("Something went wrong while loading this page.")
            }
        }
    }
}
