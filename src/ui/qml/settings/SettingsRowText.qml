import QtQuick
import LastMusicPlayer

// A Settings row's words: the 14px title, then the 12.5px description 2px
// under it, at 1.4 line height.
Item {
    id: rowText

    property string title: ""
    property string description: ""
    property bool titleDanger: false
    property bool descriptionError: false
    // A larger or semibold title (the account's name, the app's name).
    property int titleSize: 14
    property bool titleStrong: false

    // Takes the width the row gives it (Layout.fillWidth), not its words'.
    implicitWidth: 0
    implicitHeight: words.implicitHeight

    Column {
        id: words

        width: rowText.width
        spacing: 2

        Text {
            width: words.width
            text: rowText.title
            font.family: SettingsStyle.family
            font.pixelSize: rowText.titleSize
            font.weight: rowText.titleStrong ? Font.DemiBold : Font.Normal
            color: rowText.titleDanger ? SettingsStyle.dangerText : SettingsStyle.title
            elide: Text.ElideRight
        }

        Text {
            width: words.width
            visible: rowText.description !== ""
            text: rowText.description
            font.family: SettingsStyle.family
            font.pixelSize: 12
            lineHeight: 17
            lineHeightMode: Text.FixedHeight
            color: rowText.descriptionError ? Theme.danger : SettingsStyle.description
            wrapMode: Text.Wrap
        }
    }
}
