import QtQuick
import LastMusicPlayer

// The queue rail (index.html 98-105; styles.css 1176-1221): the "Up Next" /
// "Lyrics" tab strip and the area below it: the Up Next list, or lyrics
// once they exist. The shell places it as a column or over the content.
GlassSurface {
    id: rail

    // 0: Up Next, 1: Lyrics.
    // Shown from App.layout.railTab; a press writes there, never here, so
    // the rail keeps following the shared setting.
    property int tab: 0

    // padding: 8px 0 16px; the tab strip has margin 0 24px 24px.
    // Connect holds the column in place of the tabs while it is open.
    // Made only while open: it is rarely shown.
    Loader {
        anchors.fill: parent
        anchors.topMargin: 8
        anchors.bottomMargin: 16
        active: App.layout.connectOpen
        visible: active

        sourceComponent: Component {
            ConnectPanel {
                onCloseRequested: App.layout.closeConnect()
            }
        }
    }

    SegmentedControl {
        id: tabs

        visible: !App.layout.connectOpen

        x: 24
        y: 8
        width: rail.width - 2 * 24
        choices: [qsTr("Up Next"), qsTr("Lyrics")]
        // A song sent to the queue lands on the Up Next half.
        Component.onCompleted: App.effects.addQueueTarget(tabs, 0.25, 1)
        currentIndex: rail.tab
        onActivated: index => App.layout.railTab = index
    }

    // .right-scroll: the rest of the rail, padding 0 24px 16px. Up Next
    // shows the queue; Lyrics shows the playing song above its lyrics.
    Item {
        id: body

        visible: !App.layout.connectOpen

        x: 24
        y: tabs.y + tabs.height + 24
        width: rail.width - 2 * 24
        // Down to the rail's bottom edge (the player bar's top), so rows go
        // under the bar's edge; the rail's 16px padding below is space after
        // the last row inside the scroll (QueueList's bottomMargin), and the
        // idle panels keep it as a margin of their own.
        height: rail.height - y

        QueueRail {
            anchors.fill: parent
            visible: rail.tab === 0
        }

        // Made only while its tab is chosen.
        Loader {
            anchors.fill: parent
            active: rail.tab === 1
            visible: active

            sourceComponent: Component {
                LyricsRail {}
            }
        }
    }
}
