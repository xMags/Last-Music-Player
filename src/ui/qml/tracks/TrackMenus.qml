import QtQuick
import LastMusicPlayer

// The song menu of a page (App.trackMenu), with the playlist menu that
// "Add to playlist" opens in its place and the name prompt "New playlist"
// opens. A page calls open() after preparing the song.
Item {
    id: menus

    // Opens the song menu at `x`, `y` of `item`.
    function open(item: Item, x: real, y: real, fromKeyboard: bool) {
        const place = item.mapToItem(menus, x, y)
        lastPlace = Qt.point(place.x, place.y)
        songMenu.openAt(place.x, place.y)
        songMenu.keyboardActive = fromKeyboard
    }

    property point lastPlace: Qt.point(0, 0)

    anchors.fill: parent

    Connections {
        target: App.trackMenu

        function onPlaylistMenuRequested() {
            playlistMenu.openAt(menus.lastPlace.x, menus.lastPlace.y)
        }

        function onNewPlaylistRequested() {
            namePrompt.text = ""
            namePrompt.open()
        }
    }

    ActionMenu {
        id: songMenu

        items: App.trackMenu.menu
        onTriggered: itemId => App.trackMenu.trigger(itemId)
    }

    ActionMenu {
        id: playlistMenu

        items: App.trackMenu.playlistMenu
        onTriggered: itemId => App.trackMenu.trigger(itemId)
    }

    TextPromptDialog {
        id: namePrompt

        title: qsTr("New playlist")
        fieldLabel: qsTr("Name")
        placeholderText: qsTr("My playlist")
        submitLabel: qsTr("Create")
        maximumLength: 100
        onSubmitted: value => {
            App.trackMenu.createPlaylist(value)
            namePrompt.finish()
        }
    }
}
