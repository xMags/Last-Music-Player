import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The scroll bar (styles.css 1537-1546): a 10px lane with a rounded thumb
// inset 2px on each side, a little darker while pointed at in light theme.
T.ScrollBar {
    id: control

    implicitWidth: vertical ? 10 : Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: vertical ? Math.max(implicitBackgroundHeight, implicitContentHeight + topPadding + bottomPadding) : 10
    padding: 2
    visible: policy === T.ScrollBar.AlwaysOn || size < 1
    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: 3
        color: control.hovered || control.pressed ? Theme.scrollThumbHover : Theme.scrollThumb
    }
}
