import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The text field of a dialog (.modal-field input, styles.css 1018-1033): a
// thin border with a darker bottom edge that turns into a 2px accent line
// while the field has focus.
T.TextField {
    id: control

    implicitWidth: 200
    implicitHeight: contentHeight + topPadding + bottomPadding
    // padding: 9px 11px
    topPadding: 9
    bottomPadding: 9
    leftPadding: 11
    rightPadding: 11
    font: Theme.fieldInput
    color: Theme.fg
    placeholderTextColor: Theme.fgTertiary
    selectionColor: Theme.accentSoft2
    selectedTextColor: Theme.fg
    verticalAlignment: TextInput.AlignVCenter
    opacity: enabled ? 1 : 0.6

    background: Rectangle {
        radius: 4
        color: Theme.surface
        border.width: 1
        border.color: Theme.strokeStrong

        // The bottom edge of the border.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: parent.radius
            anchors.rightMargin: parent.radius
            height: control.activeFocus ? 2 : 1
            color: control.activeFocus ? Theme.accentColor : Theme.fgTertiary
        }
    }
}
