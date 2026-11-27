import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A round button of the full-screen player (.fs-btn, styles.css 2179-2192):
// 46px (52px for the skip buttons), a light glyph that is white for skips
// and the accent's light tone while `active`, a faint white disc under the
// pointer. `badge` draws the small repeat-one mark (styles.css 2333-2339).
T.Button {
    id: control

    required property string iconName
    property real iconSize: 20
    property bool skip: false
    property bool active: false
    property string badge: ""

    implicitWidth: skip ? 52 : 46
    implicitHeight: implicitWidth
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: control.iconSize
            color: control.active ? Theme.accentDark : control.skip ? Theme.fullscreenText : Theme.fullscreenButtonGlyph

            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.controlFade; easing: Motion.ease }
            }
        }

        AppText {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 7
            anchors.rightMargin: 7
            visible: control.badge !== ""
            text: control.badge
            font: Theme.fullscreenRepeatBadge
            color: Theme.accentDark
        }
    }

    background: Rectangle {
        radius: width / 2
        color: control.hovered ? Theme.fullscreenButtonHover : "transparent"

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.controlFade; easing: Motion.ease }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: width / 2
        }
    }
}
