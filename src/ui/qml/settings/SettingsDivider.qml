import QtQuick
import LastMusicPlayer

// The line between two rows of a card (.settings-divider): 1px, inset by the
// card's 20px padding on both sides.
Item {
    width: parent ? parent.width : 0
    implicitHeight: 1

    Rectangle {
        x: 20
        width: parent.width - 2 * 20
        height: 1
        color: Theme.divider
    }
}
