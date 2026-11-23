pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The Up Next tab while nothing plays (views/queue.ts 198-245): the latest
// plays (at most six, each song once) under "Recently played"; a click,
// Enter or Space plays one with the whole history to follow. With no
// history yet it says "Nothing playing yet." under "Now playing".
Item {
    id: panel

    readonly property bool hasRecent: App.queueRail.recent.count > 0

    RailSectionLabel {
        id: label

        width: panel.width
        text: panel.hasRecent ? qsTr("RECENTLY PLAYED") : qsTr("NOW PLAYING")
    }

    CompactEmptyState {
        y: label.height + 16
        width: panel.width
        visible: !panel.hasRecent
        text: qsTr("Nothing playing yet.")
    }

    // The rows reach 8px past the text on each side.
    ListView {
        id: list

        x: -8
        y: label.height + 16
        width: panel.width + 2 * 8
        height: panel.height - y
        visible: panel.hasRecent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: App.queueRail.recent

        delegate: QueueRow {
            id: row

            width: list.width
            showMore: false
            Accessible.name: qsTr("Play %1").arg(row.title)
            onClicked: App.queueRail.playRecent(row.index)
            onActivated: App.queueRail.playRecent(row.index)
        }

        WheelScroll { flickable: list }
    }
}
