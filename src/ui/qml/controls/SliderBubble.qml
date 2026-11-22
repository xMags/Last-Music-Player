import QtQuick
import LastMusicPlayer

// The small value bubble over a slider (.seek-bubble, motion.css 306-328):
// centred over the pointer and kept inside the ends of the track. Pops in with
// a little overshoot.
Item {
    id: root

    required property string text
    required property real pointerX
    required property real trackWidth
    required property bool shown

    x: Math.min(trackWidth - width, Math.max(0, pointerX - width / 2))
    width: bubble.width
    height: bubble.height
    z: 1
    opacity: shown ? 1 : 0
    scale: shown ? 1 : 0.85
    transformOrigin: Item.Bottom

    Behavior on opacity {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.bubbleFade; easing: Motion.ease }
    }
    Behavior on scale {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.bubbleScale; easing: Motion.bubblePop }
    }

    BoxShadow {
        anchors.fill: bubble
        layers: Theme.shadowBubble
        radius: bubble.radius
    }

    // padding: 4px 8px
    Rectangle {
        id: bubble

        width: label.implicitWidth + 16
        height: label.implicitHeight + 8
        radius: Theme.radiusSm
        color: Theme.fg

        Text {
            id: label

            anchors.centerIn: parent
            text: root.text
            font: Theme.bubbleLabel
            lineHeight: Theme.lineHeightBubbleLabel
            lineHeightMode: Text.FixedHeight
            color: Theme.bg
        }
    }
}
