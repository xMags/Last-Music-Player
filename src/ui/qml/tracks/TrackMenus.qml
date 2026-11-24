import QtQuick
import LastMusicPlayer

// The song menu of a page (App.trackMenu), with the playlist menu that
// "Add to playlist" opens in its place and the name prompt "New playlist"
// opens. A page calls open() after preparing the song.
Item {
    id: menus

    // Opens the song menu at `x`, `y` of `item`.
    function open(item: Item, x: real, y: real, fromKeyboard: bool) {
        arm(item, x, y)
        songMenu.openAt(place(item, x, y).x, place(item, x, y).y)
        songMenu.keyboardActive = fromKeyboard
    }

    // For a host that opens its own menu with the song items in it (the
    // queue's): the playlist menu and name prompt open here, at `x`, `y`.
    function arm(item: Item, x: real, y: real) {
        lastPlace = place(item, x, y)
        armed = true
    }

    // Lets go once whatever this opened has closed, so another page's
    // menus answer the next song menu.
    function disarmSoon() {
        Qt.callLater(() => {
            if (!songMenu.opened && !playlistMenu.opened && !namePrompt.opened)
                menus.armed = false
        })
    }

    function place(item: Item, x: real, y: real): point {
        const mapped = item.mapToItem(menus, x, y)
        return Qt.point(mapped.x, mapped.y)
    }

    property point lastPlace: Qt.point(0, 0)
    // Only the menus that opened the last song menu answer its requests;
    // several pages (and the queue) each hold one.
    property bool armed: false

    anchors.fill: parent

    Connections {
        target: App.trackMenu

        function onPlaylistMenuRequested() {
            if (!menus.armed)
                return
            playlistMenu.openAt(menus.lastPlace.x, menus.lastPlace.y)
        }

        function onNewPlaylistRequested() {
            if (!menus.armed)
                return
            namePrompt.text = ""
            namePrompt.open()
        }
    }

    ActionMenu {
        id: songMenu

        items: App.trackMenu.menu
        onTriggered: itemId => App.trackMenu.trigger(itemId)
        onClosed: menus.disarmSoon()
    }

    ActionMenu {
        id: playlistMenu

        items: App.trackMenu.playlistMenu
        onTriggered: itemId => App.trackMenu.trigger(itemId)
        onClosed: menus.disarmSoon()
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
        onClosed: menus.disarmSoon()
    }
}
