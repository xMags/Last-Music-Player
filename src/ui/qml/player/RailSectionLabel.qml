import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// A section label of the queue rail (.right-label, .right-label-rule,
// styles.css 1231-1246): 11px bold capitals with wide spacing and, with
// `rule`, a hairline running from 12px after the text to the end.
RowLayout {
    id: label

    required property string text
    property bool rule: true
    property color textColor: Theme.fgTertiary

    spacing: 12

    Text {
        text: label.text
        font: Theme.sidebarSectionLabel
        lineHeight: Theme.lineHeightSidebarSectionLabel
        lineHeightMode: Text.FixedHeight
        color: label.textColor
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        visible: label.rule
        color: Theme.stroke
    }
}
