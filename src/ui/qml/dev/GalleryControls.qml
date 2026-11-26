import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Buttons in every style and state, and the input controls.
Column {
    id: root

    required property GalleryViewModel model

    spacing: 32

    GallerySection {
        width: root.width
        title: qsTr("Buttons")

        Flow {
            width: parent.width
            spacing: 12

            AppButton { text: qsTr("Standard") }
            AppButton { text: qsTr("Accent"); variant: AppButton.Accent; iconName: "play" }
            AppButton { text: qsTr("Subtle"); variant: AppButton.Subtle }
            AppButton { text: qsTr("Danger"); variant: AppButton.Danger }
            AppButton { text: qsTr("Standard"); enabled: false }
            AppButton { text: qsTr("Accent"); variant: AppButton.Accent; enabled: false }
            AppButton { text: qsTr("Subtle"); variant: AppButton.Subtle; enabled: false }
            AppButton { text: qsTr("Danger"); variant: AppButton.Danger; enabled: false }
        }

        Flow {
            width: parent.width
            spacing: 12

            IconButton { iconName: "shuffle" }
            IconButton { iconName: "repeat"; active: true }
            IconButton { iconName: "skip-back"; enabled: false }
            IconButton { iconName: "heart"; shape: IconButton.Square }
            IconButton { iconName: "heart-fill"; shape: IconButton.Square; active: true }
            PlayButton {}
            PlayButton { playing: true }
            Chip { text: qsTr("Songs") }
            Chip { text: qsTr("Albums"); selected: true }
            Chip { text: qsTr("Artists"); enabled: false }
        }

        // The overlay button sits on artwork, so show it on a cover.
        ArtTile {
            width: 120
            height: 120
            artworkId: "gallery-overlay"
            radius: Theme.radiusXl

            IconButton {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                iconName: "more"
                shape: IconButton.Overlay
            }
        }
    }

    GallerySection {
        width: root.width
        title: qsTr("Inputs")

        RowLayout {
            spacing: 24

            AppSwitch {}
            AppSwitch { checked: true }
            AppSwitch { enabled: false }

            AppSlider {
                id: volume

                Layout.preferredWidth: 220
                sourceValue: 0.4
                bubbleText: Math.round(volume.hoverRatio * 100) + "%"
            }

            AppSlider {
                Layout.preferredWidth: 220
                sourceValue: 0.65
                playerStyle: true
            }
        }

        RowLayout {
            spacing: 24

            SegmentedControl {
                Layout.preferredWidth: 300
                choices: [qsTr("Up next"), qsTr("Lyrics")]
                onActivated: index => currentIndex = index
            }

            SegmentedControl {
                compact: true
                currentIndex: 1
                choices: [qsTr("List"), qsTr("Grid")]
                onActivated: index => currentIndex = index
            }

            AppSelect {
                model: root.model.selectChoices
            }
        }

        SearchField {
            width: Math.min(parent.width, implicitWidth)
            placeholderText: qsTr("Search songs, artists, albums")
            shortcutHint: qsTr("Ctrl + K")
            onQueryCommitted: query => root.model.report(qsTr("Search: %1").arg(query))
        }
    }
}
