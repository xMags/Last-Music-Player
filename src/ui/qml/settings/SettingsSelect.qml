import QtQuick
import LastMusicPlayer

// The dropdown of Settings: the app's dropdown in the Settings box, 36px
// high, 200px to 280px wide (a longer choice, such as a device's name, ends
// in "..."), 8px corners, a 1px border round a face a shade off the card,
// 13.5px text. The open list is the app's.
AppSelect {
    id: select

    implicitWidth: Math.min(280, Math.max(200, implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: 36
    font: Qt.font({ family: SettingsStyle.family, pixelSize: 13 })

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: 36
        radius: 8
        color: select.enabled && select.hovered ? SettingsStyle.controlHover : SettingsStyle.control
        border.width: 1
        border.color: SettingsStyle.controlBorder

        FocusRing {
            shown: select.visualFocus
            controlRadius: 8
        }
    }
}
