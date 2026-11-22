import QtQuick
import QtQuick.Controls
import LastMusicPlayer

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    minimumWidth: 768
    minimumHeight: 560
    visible: true
    title: AppInfo.displayName
    color: Theme.bg

    Gallery {
        anchors.fill: parent
    }
}
