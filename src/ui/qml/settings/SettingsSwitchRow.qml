import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// A whole row that toggles (.settings-row-switch, views.css 1167-1171): the
// row is the switch, so a click or Space anywhere on it flips the setting.
// It tints on hover and press; disabled it fades to half. `toggled` reports
// the new value; the owner writes it and binds `checked` back.
T.AbstractButton {
    id: row

    property string title: ""
    property string description: ""

    signal toggled(bool value)

    width: parent ? parent.width : 0
    implicitHeight: Math.max(64, layout.implicitHeight + 2 * 12)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.5

    onClicked: row.toggled(!row.checked)

    HoverHandler {
        cursorShape: row.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Rectangle {
        color: row.down ? Theme.hover : (row.hovered ? Theme.rowHover : "transparent")

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }

        // Inside the row, as outline-offset: -2px draws it.
        FocusRing {
            shown: row.visualFocus
            controlRadius: Theme.radiusLg
            offset: -2 - Theme.focusRingWidth
        }
    }

    contentItem: Item {
        RowLayout {
            id: layout

            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 20
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: row.title
                    font: Theme.settingsRowTitle
                    lineHeight: Theme.lineHeightSettingsRowTitle
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: row.description !== ""
                    text: row.description
                    font: Theme.settingsRowDescription
                    lineHeight: Theme.lineHeightSettingsRowDescription
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fgTertiary
                    wrapMode: Text.Wrap
                }
            }

            // Part of the row: it keeps no state of its own, and a click on
            // it toggles the row like a click anywhere else.
            AppSwitch {
                Layout.alignment: Qt.AlignVCenter
                checkable: false
                checked: row.checked
                hoverShown: row.hovered
                focusPolicy: Qt.NoFocus
                onClicked: row.toggled(!row.checked)
            }
        }
    }
}
