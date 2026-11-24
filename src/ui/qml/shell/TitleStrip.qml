import QtQuick
import LastMusicPlayer

// The title strip of the frameless window (.titlebar, styles.css 186-214;
// index.html 50-57): an empty drag area and the minimise, maximise and close
// buttons. The window chrome answers drags and the maximise button natively;
// this item tells it where the buttons are.
Rectangle {
    id: strip

    // Over the full-screen player the strip is see-through and its buttons light.
    property bool overPlayer: false
    // Over the welcome flow, which draws its own page behind the strip.
    property bool overWelcome: false

    implicitHeight: Theme.titleBarHeight
    color: App.windowChrome.backdropActive || overPlayer || overWelcome ? "transparent" : Theme.surface

    function reportButtons() {
        App.windowChrome.setCaptionButtons(minimize.mapToItem(null, 0, 0, minimize.width, minimize.height),
                                           maximize.mapToItem(null, 0, 0, maximize.width, maximize.height),
                                           close.mapToItem(null, 0, 0, close.width, close.height))
    }

    onWidthChanged: reportButtons()
    onXChanged: reportButtons()
    Component.onCompleted: {
        App.windowChrome.setDragStripHeight(Theme.titleBarHeight)
        reportButtons()
    }

    Row {
        anchors.right: parent.right
        height: parent.height

        CaptionButton {
            Accessible.name: qsTr("Minimize")
            id: minimize

            glyph: ""
            windowActive: App.windowChrome.active
            overPlayer: strip.overPlayer
            onClicked: App.windowChrome.minimize()
        }

        // ChromeRestore while maximised, ChromeMaximize otherwise.
        CaptionButton {
            Accessible.name: App.windowChrome.maximized ? qsTr("Restore") : qsTr("Maximize")
            id: maximize

            glyph: App.windowChrome.maximized ? "" : ""
            windowActive: App.windowChrome.active
            overPlayer: strip.overPlayer
            externalHover: App.windowChrome.maximizeHovered
            externalPress: App.windowChrome.maximizePressed
            onClicked: App.windowChrome.toggleMaximize()
        }

        CaptionButton {
            Accessible.name: qsTr("Close")
            id: close

            glyph: ""
            closeButton: true
            windowActive: App.windowChrome.active
            overPlayer: strip.overPlayer
            onClicked: App.windowChrome.close()
        }
    }
}
