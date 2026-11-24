import QtQuick
import QtQuick.Controls
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

        // The reference's order (index.html 135-141): Queue, Devices, Full
        // screen, Mute; each with its title as a tooltip.
        IconButton {
            Accessible.name: qsTr("Queue")
            id: queueButton

            iconName: "queue"
            active: App.layout.railOpen
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Queue")
            ToolTip.delay: 500
            onClicked: App.layout.toggleRail()
            // Where a song sent to the queue lands while the side panel is shut.
            Component.onCompleted: App.effects.addQueueTarget(queueButton, 0.5, 0)
        }

        IconButton {
            id: castButton

            readonly property string label: App.playerBar.remote ? qsTr("Playing on %1").arg(App.playerBar.playingOn)
                : App.cast.active ? qsTr("Casting to %1").arg(App.cast.deviceName)
                : qsTr("Connect to a device")

            // Always the 18px devices glyph, always offered (casting from
            // this PC is always possible).
            iconName: "devices"
            iconSize: 18
            active: App.cast.active || App.playerBar.remote || App.layout.connectOpen
            Accessible.name: label
            ToolTip.visible: hovered
            ToolTip.text: label
            ToolTip.delay: 500
            onClicked: App.layout.toggleConnect()

            // The open panel is marked with a 4px accent dot under the button.
            Rectangle {
                visible: App.layout.connectOpen
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 1
                width: 4
                height: 4
                radius: 2
                color: Theme.accentColor
            }
        }

        IconButton {
            Accessible.name: qsTr("Full screen")
            iconName: "fullscreen"
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Full screen")
            ToolTip.delay: 500
            onClicked: App.fullscreen.openPlayer()
        }

        IconButton {
            Accessible.name: App.playerBar.muted ? qsTr("Unmute") : qsTr("Mute")
            ToolTip.visible: hovered
            ToolTip.text: App.playerBar.muted ? qsTr("Unmute") : qsTr("Mute")
            ToolTip.delay: 500
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

}
