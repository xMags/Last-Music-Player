import QtQuick
import QtQuick.Controls
import LastMusicPlayer

// The main window. It starts hidden: the composition root takes over its
// frame and then shows it, so the system caption never flashes up.
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    minimumWidth: 768
    minimumHeight: 560
    visible: false
    title: AppInfo.displayName
    // See-through only once the system backdrop is confirmed on.
    color: App.windowChrome.backdropActive ? "transparent" : Theme.surface

    Loader {
        anchors.fill: parent
        sourceComponent: App.showGallery ? galleryPage : (App.firstRun.shown ? firstRun : shell)
    }

    Component {
        id: shell

        Shell {}
    }

    // The one-time music source choice of a first run.
    Component {
        id: firstRun

        FirstRunPage {}
    }

    // Started with --gallery: the design gallery below a full-width strip.
    Component {
        id: galleryPage

        Item {
            TitleStrip {
                id: strip

                width: parent.width
                visible: App.windowChrome.custom
                z: 2
            }

            Gallery {
                anchors.fill: parent
                anchors.topMargin: strip.visible ? strip.height : 0
            }
        }
    }
}
