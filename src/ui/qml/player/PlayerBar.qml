import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The player bar (index.html 109-142; styles.css 1344-1448): the current song
// on the left, the transport and seek bar in the middle (at most 500px wide),
// the queue, full screen and volume on the right. Three columns of 300px,
// flexible and 300px, 20px apart, 24px from the edges; at 899px and below the
// columns become (180px or more, 1 part), (240px or more, 1.5 parts), 230px.
// With no song loaded the seek row and the like and share buttons hide and
// skipping is greyed out.
Rectangle {
    id: bar

    readonly property bool idle: !App.playerBar.hasTrack
    readonly property bool narrow: App.layout.sidebarCollapsed
    readonly property real inner: width - 2 * 24 - 2 * 20
    readonly property real sideWidth: narrow ? 230 : 300
    readonly property real leftWidth: narrow ? Math.max(180, (inner - 230) / 2.5) : 300

    color: Theme.surface

    // border-top: 1px solid divider
    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
    }

    PlayerTrackBlock {
        id: track

        x: 24
        width: bar.leftWidth
        anchors.verticalCenter: parent.verticalCenter
        idle: bar.idle
    }

    PlayerTransport {
        x: track.x + track.width + 20 + (centreWidth - width) / 2
        width: Math.min(500, centreWidth)
        anchors.verticalCenter: parent.verticalCenter
        idle: bar.idle

        readonly property real centreWidth: bar.inner - bar.leftWidth - bar.sideWidth
    }

    // .pb-right: gap 8px, aligned to the end.
    RowLayout {
        x: bar.width - 24 - bar.sideWidth
        width: bar.sideWidth
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Item { Layout.fillWidth: true }

        IconButton {
            id: castButton

            iconName: App.cast.otherDevices ? "devices" : "cast"
            active: App.cast.active || App.playerBar.remote
            enabled: !bar.idle || App.cast.active || App.cast.otherDevices
            Accessible.name: App.playerBar.remote ? qsTr("Playing on %1").arg(App.playerBar.playingOn)
                           : App.cast.active ? qsTr("Casting to %1").arg(App.cast.deviceName)
                           : App.cast.otherDevices ? qsTr("Connect to a device") : qsTr("Cast to a device")
            onClicked: {
                App.cast.openMenu()
                castMenu.openAbove(castButton)
            }
        }

        IconButton {
            Accessible.name: qsTr("Queue")
            id: queueButton

            iconName: "queue"
            active: App.layout.railOpen
            onClicked: App.layout.toggleRail()
            // Where a song sent to the queue lands while the side panel is shut.
            Component.onCompleted: App.effects.addQueueTarget(queueButton, 0.5, 0)
        }

        IconButton {
            Accessible.name: qsTr("Full screen")
            iconName: "fullscreen"
            onClicked: App.fullscreen.openPlayer()
        }

        IconButton {
            Accessible.name: App.playerBar.muted ? qsTr("Unmute") : qsTr("Mute")
            Layout.leftMargin: 4
            iconName: App.playerBar.muted ? "volume-mute" : "volume"
            onClicked: App.playerBar.toggleMute()
        }

        AppSlider {
            id: volume

            Layout.preferredWidth: 110
            playerStyle: true
            sourceValue: App.playerBar.volumeRatio
            bubbleText: App.playerBar.volumeLabelAt(volume.hoverRatio)
            onMoved: App.playerBar.setVolumeRatio(volume.value)
        }
    }

    ActionMenu {
        id: castMenu

        items: App.cast.menu
        onTriggered: itemId => App.cast.choose(itemId)
    }
}
