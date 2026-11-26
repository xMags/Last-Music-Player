import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The Lyrics tab of the queue rail (views/queue.ts 160-172): the playing
// song above its lyrics. While nothing plays, the idle panel shows instead.
Item {
    id: rail

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: App.queueRail.hasTrack

        QueueNowPlayingHeader {
            Layout.fillWidth: true
        }

        LyricsPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
        }
    }

    QueueIdlePanel {
        anchors.fill: parent
        anchors.bottomMargin: 16
        visible: !App.queueRail.hasTrack
    }
}
