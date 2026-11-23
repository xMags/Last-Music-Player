import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The heading of a Settings group (.settings-section-head, views.css
// 1117-1136): a 24px accent-tinted chip with the icon, the title, then a rule
// to the end of the line, inset 4px. An optional note sits under it.
Column {
    id: head

    property string iconName: ""
    property string title: ""
    property string note: ""

    spacing: 6

    RowLayout {
        x: 4
        width: head.width - 2 * 4
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            radius: 7
            color: Theme.accentSoft2

            Icon {
                anchors.centerIn: parent
                name: head.iconName
                size: 13
                color: Theme.accentColor
            }
        }

        Text {
            text: head.title
            font: Theme.settingsSection
            lineHeight: Theme.lineHeightSettingsSection
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }
    }

    Text {
        x: 4
        width: head.width - 2 * 4
        visible: head.note !== ""
        text: head.note
        font: Theme.settingsRowDescription
        lineHeight: Theme.lineHeightSettingsRowDescription
        lineHeightMode: Text.FixedHeight
        color: Theme.fgTertiary
        wrapMode: Text.Wrap
    }
}
