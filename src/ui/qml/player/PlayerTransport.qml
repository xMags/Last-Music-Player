import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import LastMusicPlayer

// The middle of the player bar (.pb-center, styles.css 1386-1447): shuffle,
// previous, the play button, next and repeat 14px apart, and below them the
// seek row (elapsed time, seek bar, total), 4px apart.
ColumnLayout {
    id: transport

    required property bool idle

    spacing: 4

    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 14

        IconButton {
            Accessible.name: qsTr("Shuffle")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Shuffle")
            ToolTip.delay: 500
            iconName: "shuffle"
            active: App.playerBar.shuffle
            onClicked: App.playerBar.toggleShuffle()
        }

        // Skipping is greyed out while nothing is loaded.
        IconButton {
            Accessible.name: qsTr("Previous")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous")
            ToolTip.delay: 500
            iconName: "skip-back"
            iconSize: 20
            enabled: !transport.idle
            opacity: transport.idle ? 0.35 : 1
            onClicked: App.playerBar.previous()
        }

        PlayButton {
            playing: App.playerBar.playing
            ToolTip.visible: hovered
            ToolTip.text: App.playerBar.playing ? qsTr("Pause") : qsTr("Play")
            ToolTip.delay: 500
            onClicked: App.playerBar.togglePlay()
        }

        IconButton {
            Accessible.name: qsTr("Next")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next")
            ToolTip.delay: 500
            iconName: "skip-fwd"
            iconSize: 20
            enabled: !transport.idle
            opacity: transport.idle ? 0.35 : 1
            onClicked: App.playerBar.next()
        }

        IconButton {
            // The title says the mode (player.ts 2309).
            readonly property string label: App.playerBar.repeatMode === 2 ? qsTr("Repeat one")
                : App.playerBar.repeatMode === 1 ? qsTr("Repeat all") : qsTr("Repeat off")

            Accessible.name: label
            ToolTip.visible: hovered
            ToolTip.text: label
            ToolTip.delay: 500
            iconName: "repeat"
            active: App.playerBar.repeatMode !== 0
            onClicked: App.playerBar.cycleRepeat()

            // Repeat one: a small "1" over the glyph.
            Text {
                anchors.centerIn: parent
                topPadding: 1
                visible: App.playerBar.repeatMode === 2
                text: "1"
                font: Theme.repeatBadge
                color: Theme.accentColor
            }
        }
    }

    // .pb-seek: gap 12px. Kept in place but hidden while idle.
    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        opacity: transport.idle ? 0 : 1
        enabled: !transport.idle

        Text {
            Layout.minimumWidth: 32
            horizontalAlignment: Text.AlignHCenter
            text: App.playerBar.elapsedText
            font: Theme.playerTime
            color: Theme.fgTertiary
        }

        AppSlider {
            id: seek

            Layout.fillWidth: true
            playerStyle: true
            sourceValue: App.playerBar.progress
            bubbleText: App.playerBar.seekLabelAt(seek.hoverRatio)
            // The song follows the drag; the bar ignores playback while held.
            onMoved: App.playerBar.seekToRatio(seek.value)
        }

        Text {
            Layout.minimumWidth: 32
            horizontalAlignment: Text.AlignHCenter
            text: App.playerBar.totalText
            font: Theme.playerTime
            color: Theme.fgTertiary
        }
    }
}
