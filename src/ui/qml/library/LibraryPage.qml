pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The Library page (views/library.ts): the eyebrow and title, the tabs, then
// the chosen tab: cards for Playlists, Albums, Artists and Genres; songs in
// the saved List or Grid layout for History and Songs, with their toolbar
// (layout switch, count, sort; Songs also filters). The page fills the panel
// and its lists scroll themselves, so a long Songs list stays smooth. An
// empty library offers to choose a music folder.
Item {
    id: page

    readonly property real gutter: 32
    // The scrolling list of the shown tab (each tab view sets it).
    property Flickable currentView: null
    readonly property bool trackTab: App.library.tab === "history" || App.library.tab === "songs"

    Component.onCompleted: App.library.refresh()

    // The page heading and tabs, at the top of every tab's scrolling list:
    // the eyebrow and title with New playlist (and, on Albums, the current
    // app's New album) at the right, then the tabs.
    Component {
        id: heading

        Column {
            width: parent ? parent.width : 0
            spacing: 0

            Item { width: 1; height: 24 }

            Item {
                width: parent.width
                height: titleBlock.implicitHeight

                Column {
                    id: titleBlock

                    x: 4

                    Text {
                        text: qsTr("YOUR COLLECTION")
                        font: Theme.eyebrow
                        color: Theme.fgTertiary
                    }

                    Text {
                        text: qsTr("Library")
                        font: Theme.settingsHeading
                        lineHeight: Theme.lineHeightSettingsHeading
                        lineHeightMode: Text.FixedHeight
                        color: Theme.fg
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    spacing: 8

                    AppButton {
                        visible: App.library.tab === "albums"
                        text: qsTr("New album")
                        iconName: "plus"
                        iconSize: 14
                        onClicked: App.collections.requestCreateAlbum()
                    }

                    AppButton {
                        text: qsTr("New playlist")
                        iconName: "plus"
                        iconSize: 14
                        onClicked: App.collections.requestCreatePlaylist()
                    }
                }
            }

            Item { width: 1; height: 22 }

            SegmentedControl {
                compact: true
                choices: App.library.tabs.map(tab => tab.label)
                currentIndex: App.library.tabs.findIndex(tab => tab.value === App.library.tab)
                onActivated: index => App.library.tab = App.library.tabs[index].value
            }

            Item { width: 1; height: 20 }
        }
    }

    // The toolbar of History and Songs.
    Component {
        id: trackToolbar

        Column {
            width: parent ? parent.width : 0

            Loader {
                width: parent.width
                sourceComponent: heading
            }

            RowLayout {
                width: parent.width
                spacing: 10

                SearchField {
                    Layout.preferredWidth: 280
                    visible: App.library.tab === "songs"
                    placeholderText: qsTr("Filter songs")
                    text: App.library.songsFilter
                    onQueryCommitted: query => App.library.songsFilter = query
                    onTextChanged: filterDelay.restart()

                    Timer {
                        id: filterDelay

                        interval: 350
                        onTriggered: App.library.songsFilter = parent.text
                    }
                }

                Item { Layout.fillWidth: true }

                SegmentedControl {
                    compact: true
                    choices: [qsTr("List"), qsTr("Grid")]
                    currentIndex: App.settings.trackView === "list" ? 0 : 1
                    onActivated: index => App.settings.trackView = index === 0 ? "list" : "grid"
                }

                CountPill {
                    count: App.library.tab === "songs" ? App.library.songs.totalCount : App.library.history.count
                }

                AppSelect {
                    model: App.library.tab === "songs" ? App.library.songsSorts : App.library.historySorts
                    currentIndex: App.library.tab === "songs" ? App.library.songsSort : App.library.historySort
                    onActivated: index => {
                        if (App.library.tab === "songs")
                            App.library.songsSort = index
                        else
                            App.library.historySort = index
                    }
                }
            }

            Item { width: 1; height: 20 }
        }
    }

    ScrollMemory {
        target: page.currentView
    }

    Loader {
        anchors.fill: parent
        sourceComponent: App.library.empty ? emptyLibrary : (page.trackTab ? trackTabView : cardTabView)
    }

    Component {
        id: trackTabView

        TrackListView {
            id: songList

            readonly property string listName: App.library.tab

            onViewChanged: page.currentView = songList.view
            Component.onCompleted: page.currentView = songList.view

            model: App.library.tab === "songs" ? App.library.songs : App.library.history
            grid: App.settings.trackView !== "list"
            sidePadding: page.gutter
            header: trackToolbar
            onActivated: row => App.library.playSong(songList.listName, row)
            onLikeRequested: row => App.library.likeSong(songList.listName, row)
            onMenuRequested: (row, at, fromKeyboard) => {
                App.library.prepareSongMenu(songList.listName, row)
                menus.open(songList, at.x, at.y, fromKeyboard)
            }
        }
    }

    Component {
        id: cardTabView

        Item {
            id: cardTab

            readonly property CollectionCardModel cards: App.library.tab === "playlists" ? App.library.playlists
                : App.library.tab === "albums" ? App.library.albums
                : App.library.tab === "artists" ? App.library.artists
                : App.library.genres

            Component.onCompleted: page.currentView = grid

            CardGrid {
                id: grid

                x: page.gutter
                width: parent.width - 2 * page.gutter
                height: parent.height
                bottomMargin: 32
                scrollBarHost: cardTab
                model: cardTab.cards

                header: Column {
                    width: grid.width

                    Loader {
                        width: parent.width
                        sourceComponent: heading
                    }

                    EmptyState {
                        width: parent.width
                        visible: cardTab.cards.count === 0
                        iconName: "library"
                        title: App.library.tab === "genres" ? qsTr("No genres yet") : qsTr("Nothing here yet")
                        text: App.library.tab === "genres"
                            ? qsTr("No genre tags in your library yet. Genres appear as tagged tracks arrive.")
                            : qsTr("Play or like some music to build your collection.")
                    }
                }

                delegate: Item {
                    id: cell

                    required property int index
                    required property string kind
                    required property string title
                    required property string subtitle
                    required property url artworkUrl
                    required property string artworkId
                    required property string artworkLabel
                    required property bool round
                    required property color gradientStart
                    required property color gradientEnd
                    required property string iconName
                    required property string key

                    width: grid.cellWidth
                    height: grid.cellHeight

                    CollectionCard {
                        width: grid.cardWidth
                        kind: cell.kind
                        title: cell.title
                        subtitle: cell.subtitle
                        artworkUrl: cell.artworkUrl
                        artworkId: cell.artworkId
                        artworkLabel: cell.artworkLabel
                        round: cell.round
                        gradientStart: cell.gradientStart
                        gradientEnd: cell.gradientEnd
                        iconName: cell.iconName
                        onClicked: App.library.openCard(App.library.tab, cell.index)
                        onMenuRequested: (at, fromKeyboard) => {
                            if (cell.kind !== "playlist" && cell.kind !== "album-collection")
                                return
                            page.menuKind = cell.kind
                            page.menuKey = cell.key
                            page.menuTitle = cell.title
                            cardMenuItems.clear()
                            if (cell.kind === "playlist") {
                                cardMenuItems.addItem("rename", qsTr("Rename playlist"))
                                cardMenuItems.addItem("delete", qsTr("Delete playlist"), "trash", true)
                            } else {
                                cardMenuItems.addItem("delete", qsTr("Delete album"), "trash", true)
                            }
                            const place = mapToItem(page, at.x, at.y)
                            cardMenu.openAt(place.x, place.y)
                            cardMenu.keyboardActive = fromKeyboard
                        }
                    }
                }
            }
        }
    }

    Component {
        id: emptyLibrary

        Column {
            Loader {
                x: page.gutter
                width: parent.width - 2 * page.gutter
                sourceComponent: heading
            }

            EmptyState {
                width: parent.width
                iconName: "music-note"
                title: qsTr("Your library is empty")
                text: qsTr("Choose the folder your music is in, and it will appear here.")
                actionText: qsTr("Choose music folder")
                onActionClicked: App.librarySettings.changeFolder()
            }
        }
    }

    // The menu of a user playlist's or album's card.
    property string menuKind: ""
    property string menuKey: ""
    property string menuTitle: ""

    MenuItemListModel {
        id: cardMenuItems
    }

    ActionMenu {
        id: cardMenu

        items: cardMenuItems
        onTriggered: itemId => {
            if (itemId === "rename")
                App.collections.requestRename(page.menuKey, page.menuTitle)
            else if (itemId === "delete")
                App.collections.requestDelete(page.menuKind, page.menuKey, page.menuTitle)
        }
    }

    TrackMenus {
        id: menus
    }
}
