import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The button of Settings: 36px high, padded 16px, 8px corners, a 1px border
// round a face a shade off the card, 13.5px text; hover lightens the face.
// `primary` is filled with the accent and dark semibold text; `danger` has a
// red border, a faint red face and red text; `link` is the accent text
// alone. Small is 28px high, padded 12px. While `working` the label follows
// a spinner.
T.Button {
    id: control

    property bool small: false
    property bool primary: false
    property bool danger: false
    property bool link: false
    property bool working: false
    // Optional icon after the text (an outbound link's arrow), by Icons name.
    property string trailingIconName: ""

    readonly property bool lit: enabled && (hovered || down)
    readonly property color ink: control.primary ? Theme.accentInk
        : control.danger ? SettingsStyle.dangerText
        : control.link ? Theme.accentColor
        : SettingsStyle.title

    implicitHeight: small ? 28 : 36
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    leftPadding: link ? 0 : (small ? 12 : 16)
    rightPadding: link ? 0 : (small ? 12 : 16)
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font.family: SettingsStyle.family
    font.pixelSize: control.small ? 12 : 13
    font.weight: control.primary ? Font.DemiBold : Font.Normal

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Row {
        spacing: control.spacing

        SettingsSpinner {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.working
        }

        AppText {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.link && control.lit ? Theme.mixSrgb(Theme.accentColor, Theme.fg, 0.75) : control.ink
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.trailingIconName !== ""
            name: control.trailingIconName
            size: 13
            color: control.ink
        }
    }

    background: Rectangle {
        implicitHeight: control.implicitHeight
        radius: 8
        visible: !control.link
        border.width: 1
        border.color: control.primary ? Theme.accentColor
            : control.danger ? SettingsStyle.dangerButtonBorder
            : SettingsStyle.controlBorder
        color: {
            if (control.primary)
                return control.lit ? Theme.mixSrgb(Theme.accentColor, Theme.fg, 0.9) : Theme.accentColor
            if (control.danger)
                return control.lit ? Theme.mixSrgb(Theme.danger, Theme.surface, 0.1) : SettingsStyle.dangerButton
            return control.lit ? SettingsStyle.controlHover : SettingsStyle.control
        }

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.settingsFade; easing: Motion.ease }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: 8
        }
    }
}
