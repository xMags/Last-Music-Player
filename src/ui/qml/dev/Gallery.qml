pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The design system on one page, for judging it by eye: theme and accent
// choice at the top, then every token, icon and control.
Rectangle {
    id: root

    color: Theme.bg

    GalleryViewModel {
        id: gallery
    }

    Flickable {
        id: page

        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * 32
        boundsBehavior: Flickable.StopAtBounds

        AppScrollBar.vertical: AppScrollBar {}

        Column {
            id: column

            x: 32
            y: 32
            width: page.width - 2 * 32
            spacing: 40

            Row {
                spacing: 24

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Design gallery")
                    font: Theme.settingsHeading
                    lineHeight: Theme.lineHeightSettingsHeading
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                }

                SegmentedControl {
                    anchors.verticalCenter: parent.verticalCenter
                    compact: true
                    choices: [qsTr("Light"), qsTr("Dark"), qsTr("System")]
                    currentIndex: Theme.mode
                    onActivated: index => Theme.mode = index
                }

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Repeater {
                        model: Theme.accentSeeds

                        AccentSwatch {
                            required property color modelData
                            required property int index

                            seed: modelData
                            selected: Theme.accent === index
                            onClicked: Theme.accent = index
                        }
                    }
                }
            }

            GalleryPanels { width: column.width }
            GalleryTokens { width: column.width }
            GalleryIcons { width: column.width }
            GalleryControls { width: column.width; model: gallery }
            GalleryOverlays { width: column.width; model: gallery }
            GalleryArtwork { width: column.width; model: gallery }
            GalleryLists { width: column.width; model: gallery }
        }
    }

    ToastStack {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 104
        queue: gallery.toasts
    }
}
