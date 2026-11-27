import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A text field of Settings: 36px high, padded 12px, 8px corners, a 1px
// border round the page colour (the accent while typing in it), 13.5px text.
T.TextField {
    id: control

    implicitWidth: 220
    implicitHeight: 36
    leftPadding: 12
    rightPadding: 12
    verticalAlignment: TextInput.AlignVCenter
    font.family: SettingsStyle.family
    font.pixelSize: 13
    color: SettingsStyle.title
    placeholderTextColor: SettingsStyle.description
    selectionColor: Theme.accentSoft
    selectedTextColor: SettingsStyle.title
    opacity: enabled ? 1 : Theme.disabledOpacity

    background: Rectangle {
        implicitWidth: 220
        implicitHeight: 36
        radius: 8
        color: SettingsStyle.field
        border.width: 1
        border.color: control.activeFocus ? Theme.accentColor : SettingsStyle.controlBorder
    }
}
