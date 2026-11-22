import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The range slider (.slider, styles.css 1449-1495): a 4px track filled with the
// accent up to the value and a round accent handle in a surface ring.
//
// `sourceValue` is the value owned outside (a playback position, a volume). It
// is shown only while the slider is not pressed, so outside updates never move
// the handle under the pointer. `committed` reports the value at release.
//
// `playerStyle` is the player bar's look: no handle except for keyboard focus,
// a track that thickens to 6px while pointed at, and a fill that glides to new
// values. `bubbleText`, when set, shows in a bubble over the pointer; read
// `hoverRatio` (0 to 1) to decide what it says.
T.Slider {
    id: control

    property real sourceValue: 0
    property bool playerStyle: false
    property string bubbleText: ""
    readonly property real hoverRatio: hover.hovered && width > 0 ? Math.max(0, Math.min(1, hover.point.position.x / width)) : 0
    readonly property bool engaged: hovered || pressed || visualFocus

    signal committed(real value)

    implicitWidth: 200
    implicitHeight: 16
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity

    onPressedChanged: {
        if (!pressed)
            committed(value)
    }

    Binding {
        control.value: control.sourceValue
        when: !control.pressed
        restoreMode: Binding.RestoreNone
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
    }

    background: Item {
        implicitWidth: 200
        implicitHeight: 16

        Rectangle {
            id: track

            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: control.playerStyle && control.engaged ? 6 : 4
            radius: height / 2
            color: Theme.sliderTrack

            Rectangle {
                height: parent.height
                radius: parent.radius
                color: Theme.accentColor
                width: control.visualPosition * parent.width

                Behavior on width {
                    enabled: control.playerStyle && !control.pressed && !Motion.reduced
                    NumberAnimation { duration: Motion.sliderGlide; easing: Motion.water }
                }
            }
        }
    }

    handle: Item {
        x: control.visualPosition * (control.availableWidth - width)
        y: (control.availableHeight - height) / 2
        width: 16
        height: 16
        opacity: control.playerStyle && !control.visualFocus ? 0 : 1

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowSliderThumb
            radius: width / 2
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.accentColor
            border.color: Theme.surface
            border.width: control.pressed ? 4 : control.hovered ? 2 : 3

            Behavior on border.width {
                enabled: !Motion.reduced
                NumberAnimation { duration: Motion.sliderThumb; easing: Motion.standard }
            }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: width / 2
        }
    }

    SliderBubble {
        anchors.bottom: parent.top
        anchors.bottomMargin: 8
        text: control.bubbleText
        pointerX: hover.point.position.x
        trackWidth: control.width
        shown: hover.hovered && control.bubbleText !== ""
    }
}
