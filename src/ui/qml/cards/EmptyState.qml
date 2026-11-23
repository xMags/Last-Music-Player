import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// A page or section with nothing to show (.search-desktop-status, views.css
// 124-134): an accent icon, a title, a sentence and, when there is something
// to do about it, a button. Centred, 72px of space above and below.
ColumnLayout {
    id: state

    property string iconName: "music-note"
    property string title: ""
    property string text: ""
    property string actionText: ""
    // The danger colour for the icon (ErrorState).
    property bool error: false

    signal actionClicked()

    spacing: 0

    Item { Layout.preferredHeight: 72 }

    Icon {
        Layout.alignment: Qt.AlignHCenter
        name: state.iconName
        size: 32
        color: state.error ? Theme.danger : Theme.accentColor
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 16
        Layout.maximumWidth: 420
        visible: state.title !== ""
        horizontalAlignment: Text.AlignHCenter
        text: state.title
        font: Theme.stateTitle
        color: Theme.fg
        wrapMode: Text.Wrap
    }

    Text {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        Layout.maximumWidth: 360
        visible: state.text !== ""
        horizontalAlignment: Text.AlignHCenter
        text: state.text
        font: Theme.stateText
        lineHeight: Theme.lineHeightStateText
        lineHeightMode: Text.FixedHeight
        color: Theme.fgSecondary
        wrapMode: Text.Wrap
    }

    AppButton {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 16
        visible: state.actionText !== ""
        text: state.actionText
        variant: AppButton.Accent
        onClicked: state.actionClicked()
    }

    Item { Layout.preferredHeight: 72 }
}
