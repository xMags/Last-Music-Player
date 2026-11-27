import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// A whole row that toggles: the row is the switch, so a click or Space
// anywhere on it flips the setting; disabled it fades to half. Laid out as
// SettingsRow. `changeRequested` reports the new value; the owner writes it
// and binds `checked` back.
T.AbstractButton {
    id: row

    property string title: ""
    property string description: ""
    property bool shown: true

    signal changeRequested(bool value)

    width: parent ? parent.width : 0
    visible: shown && search.matches
    implicitHeight: 1 + 14 + Math.max(44, layout.implicitHeight) + 14
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.5

    onClicked: row.changeRequested(!row.checked)

    SettingsSearchable {
        id: search

        row: row
        searchText: row.title + "\n" + row.description
    }

    HoverHandler {
        cursorShape: row.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Item {
        FocusRing {
            shown: row.visualFocus
            controlRadius: 12
            offset: -2 - Theme.focusRingWidth
        }
    }

    contentItem: Item {
        RowLayout {
            id: layout

            x: 20
            y: 15
            width: parent.width - 2 * 20
            height: Math.max(44, implicitHeight)
            spacing: 16

            SettingsRowText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                title: row.title
                description: row.description
            }

            // Part of the row: a click on it toggles the row like a click
            // anywhere else.
            SettingsSwitch {
                Layout.alignment: Qt.AlignVCenter
                checkable: false
                checked: row.checked
                focusPolicy: Qt.NoFocus
                onClicked: row.changeRequested(!row.checked)
            }
        }
    }
}
