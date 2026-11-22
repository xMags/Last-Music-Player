import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One toast (.toast, styles.css 1875-1891): a message and an optional action
// button on a raised surface. Error toasts are red with a red border.
Item {
    id: root

    required property string text
    required property bool error
    required property string actionLabel

    signal actionPressed()

    // The action button's share of a row: its width plus the 16px gap.
    readonly property real actionSpace: action.visible ? action.implicitWidth + 16 : 0

    // max-width 420px; padding 10px 16px; gap 16px.
    implicitWidth: Math.min(420, message.implicitWidth + actionSpace + 32)
    implicitHeight: Math.max(message.implicitHeight, action.visible ? action.implicitHeight : 0) + 20
    height: implicitHeight

    BoxShadow {
        anchors.fill: parent
        layers: Theme.shadowToast
        radius: Theme.radiusMd
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMd
        color: Theme.surface
        border.width: 1
        border.color: root.error ? Theme.mixSrgb(Theme.danger, "transparent", 0.5) : Theme.stroke
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 16

        Text {
            id: message

            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - root.actionSpace
            text: root.text
            font: Theme.toastLabel
            color: root.error ? Theme.danger : Theme.fg
            wrapMode: Text.Wrap
        }

        // padding: 4px 0
        T.AbstractButton {
            id: action

            anchors.verticalCenter: parent.verticalCenter
            visible: root.actionLabel !== ""
            implicitWidth: actionText.implicitWidth
            implicitHeight: actionText.implicitHeight + 8
            focusPolicy: Qt.StrongFocus
            onClicked: root.actionPressed()

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            contentItem: Text {
                id: actionText

                text: root.actionLabel
                font: Theme.toastAction
                color: Theme.accentColor
                verticalAlignment: Text.AlignVCenter
            }

            background: Item {
                FocusRing {
                    shown: action.visualFocus
                }
            }
        }
    }
}
