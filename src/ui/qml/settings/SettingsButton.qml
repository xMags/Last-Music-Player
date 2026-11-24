import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The button of Settings (.settings-button, views.css 1174-1206): 32px high,
// padded 16px, a 1px strong stroke on the alternate surface, 13px semibold
// text; hover fills with the hover colour, a press with the row hover colour.
// Small is 28px high, padded 12px, 12px text. Danger has a faint danger
// border on the surface and danger text. While `working` the label follows a
// spinner, as the reference's "Working…" buttons do.
T.Button {
    id: control

    property bool small: false
    property bool danger: false
    property bool working: false
    // Optional icon after the text (an outbound link's arrow), by Icons name.
    property string trailingIconName: ""

    readonly property bool lit: enabled && (hovered || down)

    implicitHeight: small ? 28 : 32
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    leftPadding: small ? 12 : 16
    rightPadding: small ? 12 : 16
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font: small ? Theme.settingsButtonSmallLabel : Theme.settingsButtonLabel

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    contentItem: Row {
        spacing: control.spacing

        SettingsSpinner {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.working
        }

        Text {
            id: label

            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.danger ? Theme.danger : Theme.fg
        }

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.trailingIconName !== ""
            name: control.trailingIconName
            size: 13
            color: label.color
        }
    }

    background: ButtonSurface {
        id: surface

        implicitHeight: control.implicitHeight
        radius: Theme.radiusSm
        border.width: 1
        border.color: control.danger ? Theme.mixSrgb(Theme.danger, "transparent", 0.32) : Theme.strokeStrong
        color: {
            if (control.danger)
                return control.lit ? Theme.mixSrgb(Theme.danger, Theme.surface, 0.08) : Theme.surface
            if (control.enabled && control.down)
                return Theme.rowHover
            return control.lit ? Theme.hover : Theme.surfaceAlt
        }
        fadeDuration: Motion.settingsFade
        fadeEasing: Motion.ease
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }
}
