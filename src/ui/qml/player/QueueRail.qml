import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The Up Next tab of the queue rail (views/queue.ts 141-197): the playing
// song, the Up Next header and the rows scroll together as one column
// (.right-scroll: no scroll bar, 16px of space at the end). While nothing
// plays, the idle panel shows instead.
Item {
    id: rail

    QueueList {
        anchors.fill: parent
        anchors.leftMargin: -8
        anchors.rightMargin: -8
        visible: App.queueRail.hasTrack
        bottomMargin: 16
        listHeader: ColumnLayout {
            spacing: 0

            QueueNowPlayingHeader {
                Layout.fillWidth: true
            }

            QueueSectionHeader {
                Layout.fillWidth: true
                Layout.bottomMargin: 10
            }
        }
    }

    // Made only while nothing plays.
    Loader {
        anchors.fill: parent
        anchors.bottomMargin: 16
        active: !App.queueRail.hasTrack
        visible: active

        sourceComponent: Component {
            QueueIdlePanel {}
        }
    }
}
