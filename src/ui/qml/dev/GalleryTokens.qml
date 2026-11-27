pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Every colour token as a named swatch, and every text style as a sample line.
Column {
    id: root

    spacing: 32

    GallerySection {
        width: root.width
        title: qsTr("Colours")

        GridView {
            width: parent.width
            height: contentHeight
            interactive: false
            cellWidth: 190
            cellHeight: 64
            model: ThemeTokenListModel {
                theme: Theme
                kind: ThemeTokenListModel.Colors
            }

            delegate: Row {
                id: swatch

                required property string name
                required property color value

                spacing: 10

                Rectangle {
                    width: 44
                    height: 44
                    radius: Theme.radiusMd
                    color: swatch.value
                    border.width: 1
                    border.color: Theme.stroke
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter

                    AppText {
                        text: swatch.name
                        font: Theme.trackTitle
                        color: Theme.fg
                    }
                    AppText {
                        text: swatch.value.toString()
                        font: Theme.trackArtist
                        color: Theme.fgTertiary
                    }
                }
            }
        }
    }

    GallerySection {
        width: root.width
        title: qsTr("Text styles")

        ListView {
            width: parent.width
            height: contentHeight
            interactive: false
            spacing: 12
            model: ThemeTokenListModel {
                theme: Theme
                kind: ThemeTokenListModel.Fonts
            }

            delegate: Row {
                id: style

                required property string name
                required property font value

                spacing: 24

                AppText {
                    width: 220
                    anchors.verticalCenter: parent.verticalCenter
                    text: style.name
                    font: Theme.trackArtist
                    color: Theme.fgTertiary
                }
                AppText {
                    text: qsTr("Music for every moment")
                    font: style.value
                    color: Theme.fg
                }
            }
        }
    }
}
