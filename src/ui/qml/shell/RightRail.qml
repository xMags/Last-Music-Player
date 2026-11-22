import QtQuick
import LastMusicPlayer

// The queue rail (index.html 98-105; styles.css 1176-1221): the "Up Next" /
// "Lyrics" tab strip and the area below it: the Up Next list, or lyrics
// once they exist. The shell places it as a column or over the content.
Rectangle {
    id: rail

    // 0: Up Next, 1: Lyrics.
    property int tab: 0

    color: Theme.surface

    // padding: 8px 0 16px; the tab strip has margin 0 24px 24px.
    SegmentedControl {
        id: tabs

        x: 24
        y: 8
        width: rail.width - 2 * 24
        choices: [qsTr("Up Next"), qsTr("Lyrics")]
        currentIndex: rail.tab
        onActivated: index => rail.tab = index
    }

    // .right-scroll: the rest of the rail, padding 0 24px 16px. Up Next
    // shows the queue; Lyrics stays empty until lyrics exist.
    Item {
        id: body

        x: 24
        y: tabs.y + tabs.height + 24
        width: rail.width - 2 * 24
        height: rail.height - y

        QueueRail {
            anchors.fill: parent
            visible: rail.tab === 0
        }
    }
}
