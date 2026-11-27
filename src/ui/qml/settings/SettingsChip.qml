import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A small pill of Settings: 28px high, padded 12px, 12.5px text on a face
// one shade off the card. `selected` false draws it as an outline only.
// `toggles` makes it a button (the file formats while editing).
T.AbstractButton {
    id: chip

    property bool selected: true
    property bool toggles: false

    implicitWidth: label.implicitWidth + 2 * 12
    implicitHeight: 28
    hoverEnabled: toggles
    focusPolicy: toggles ? Qt.StrongFocus : Qt.NoFocus
    enabled: toggles

    HoverHandler {
        enabled: chip.toggles
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: 14
        color: chip.selected ? SettingsStyle.line : (chip.hovered ? SettingsStyle.control : "transparent")
        border.width: chip.selected ? 0 : 1
        border.color: SettingsStyle.quiet

        FocusRing {
            shown: chip.visualFocus
            controlRadius: 14
        }
    }

    contentItem: AppText {
        id: label

        text: chip.text
        font.family: SettingsStyle.family
        font.pixelSize: 12
        color: chip.selected ? SettingsStyle.chipText : SettingsStyle.description
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
