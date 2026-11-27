import QtQuick
import LastMusicPlayer

// A small count beside a toolbar (.section-pill, styles.css 488-501): "12
// SONGS" in 10px capitals on the neutral surface, in a rounded border.
Rectangle {
    id: pill

    property int count: 0
    property string text: count === 1 ? qsTr("1 song") : qsTr("%1 songs").arg(count)

    implicitWidth: label.implicitWidth + 2 * 9
    implicitHeight: label.implicitHeight + 2 * 3
    radius: height / 2
    color: Theme.neutral
    border.width: 1
    border.color: Theme.stroke

    AppText {
        id: label

        anchors.centerIn: parent
        text: pill.text
        font: Theme.pill
        color: Theme.fgSecondary
    }
}
