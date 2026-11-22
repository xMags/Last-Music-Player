import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The side panel of the full-screen player (.fs-side, .fs-panel,
// styles.css 2253-2287): the Lyrics / Up Next switch across the top, 12px
// above a glass panel with a 20px radius and 20px of padding (none below).
// Lyrics stay empty until lyrics exist. Up Next shows the same list as the
// queue rail (views/queue.ts 268-296): its header stays put while the rows
// scroll below it with 20px of space at the end and a thin scroll bar.
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
            spacing: 0

            QueueSectionHeader {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
                rule: false
                labelColor: Theme.fullscreenTertiaryText
            }

            // The rows reach 8px past the panel's padding on each side.
            QueueList {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: -8
                Layout.rightMargin: -8
                onDark: true
                showScrollBar: true
                bottomMargin: 20
            }
        }
    }
}
