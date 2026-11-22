pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The side panel of the full-screen player (.fs-side, .fs-panel,
// styles.css 2253-2284): the Lyrics / Up Next switch across the top, 12px
// above a glass panel with a 20px radius and 20px of padding (none below).
// Lyrics stay empty until lyrics exist. Up Next shows the songs to come,
// one line each, until the queue list replaces this simple list.
ColumnLayout {
    id: side

    spacing: 12

    FullscreenTabs {
        Layout.fillWidth: true
        stretch: true
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        radius: Theme.fullscreenPanelRadius
        color: Theme.fullscreenPanelFill
        border.width: 1
        border.color: Theme.fullscreenPanelBorder

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            anchors.topMargin: 20
            visible: App.fullscreen.queueChosen
            spacing: 12

            Text {
                text: qsTr("NEXT IN QUEUE")
                font: Theme.sidebarSectionLabel
                color: Theme.fullscreenTertiaryText
            }

            Text {
                Layout.fillWidth: true
                visible: !App.fullscreen.hasUpcoming
                text: qsTr("Your queue is clear.")
                font: Theme.body
                color: Theme.fullscreenEmptyText
                wrapMode: Text.Wrap
            }

            ListView {
                id: list

                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: App.fullscreen.upcoming

                delegate: Text {
                    required property string display

                    width: list.width
                    topPadding: 8
                    bottomPadding: 8
                    text: display
                    font: Theme.trackTitle
                    color: Theme.fullscreenSecondaryText
                    elide: Text.ElideRight
                }
            }
        }
    }
}
