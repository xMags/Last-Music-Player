pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane
    // The scrolling list of the shown tab (each tab view sets it).
    property Flickable currentView: null
    // The reference's five tabs; the current app's Songs (local files) outside
    // Account mode.
    readonly property var tabs: App.library.tabs.filter(tab => tab.value !== "songs" || !App.capabilities.browseCatalogue)
    readonly property bool trackTab: App.library.tab === "history" || App.library.tab === "songs"

    Component.onCompleted: {
        if (App.library.tab === "songs" && App.capabilities.browseCatalogue)
            App.library.tab = "playlists"
        App.library.refresh()
    }

    // The page heading and tabs, at the top of every tab's scrolling list:
    // the eyebrow and title with New playlist (and, on Albums, the current
    // app's New album) at the right, then the tabs.
    Component {
        id: heading

        Column {
            width: parent ? parent.width : 0
            spacing: 0

            // .view-header: padding 18px 0 22px; the title and the buttons
            // meet at the bottom (align-items: flex-end).
            Item { width: 1; height: 18 }

            Item {
                width: parent.width
                height: Math.max(titleText.implicitHeight, buttons.implicitHeight)

                Text {
                    id: titleText

                    anchors.bottom: parent.bottom
                    text: qsTr("Library")
                    font: Theme.viewTitle
                    color: Theme.fg
                }

                Row {
                    id: buttons

                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    spacing: 10

                    // "Import link" on every tab (an album or playlist link);
                    // API key mode imports albums from a provider link.
                    AppButton {
                        visible: App.capabilities.importFromLink
                        text: App.capabilities.browseCatalogue ? qsTr("Import link")
                            : App.library.tab === "albums" ? qsTr("Import album") : qsTr("Import playlist")
                        iconName: "link"
                        iconSize: 14
                        onClicked: App.collections.requestImportLink(
                            App.capabilities.browseCatalogue ? "link" : App.library.tab === "albums" ? "album" : "playlist")
                    }

                    // The current app's local albums; the account's albums come
                    // from the catalogue instead.
                    AppButton {
                        visible: App.library.tab === "albums" && !App.capabilities.browseCatalogue
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
                choices: page.tabs.map(tab => tab.label)
                currentIndex: page.tabs.findIndex(tab => tab.value === App.library.tab)
                onActivated: index => App.library.tab = page.tabs[index].value
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
        rightSidePadding: page.rightGutter
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

            // In Account mode the Artists tab lists the account service's artists.
            readonly property bool serverArtists: App.library.tab === "artists" && App.accountArtists.active
            readonly property CollectionCardModel cards: App.library.tab === "playlists" ? App.library.playlists
                : App.library.tab === "albums" ? App.library.albums
                : App.library.tab === "artists" ? (cardTab.serverArtists ? App.accountArtists.artists : App.library.artists)
                : App.library.genres

            Component.onCompleted: {
                page.currentView = grid
                if (cardTab.serverArtists)
                    App.accountArtists.reload()
            }

            Connections {
                target: App.library

                function onTabChanged() {
                    if (cardTab.serverArtists)
                        App.accountArtists.reload()
                }
            }

            CardGrid {
                id: grid

                x: page.gutter
                width: parent.width - (page.gutter + page.rightGutter)
                height: parent.height
                bottomMargin: 32
                scrollBarHost: cardTab
                model: cardTab.cards
                // Account artists carry a Follow link under their caption.
                textHeight: cardTab.serverArtists ? 22 + 18 + 5 + 18 : 22 + 18

                // More of the account's artists, and what went wrong.
                footer: Column {
                    width: grid.width
                    visible: cardTab.serverArtists
                    spacing: 12
                    topPadding: 8

                    Text {
                        visible: App.accountArtists.status !== ""
                        text: App.accountArtists.status
                        color: Theme.fgSecondary
                        font: Theme.stateText
                    }

                    AppButton {
                        visible: App.accountArtists.hasMore
                        enabled: !App.accountArtists.loading
                        text: App.accountArtists.loading ? qsTr("Loading more artists...") : qsTr("Load more")
                        onClicked: App.accountArtists.loadMore()
                    }
                }

                header: Column {
                    width: grid.width

                    Loader {
                        width: parent.width
                        sourceComponent: heading
                    }

                    // .lib-playlist-filter: "Your playlists" or "Auto mixes",
                    // 4px apart; padding-bottom 20px and margin-bottom 16px.
                    Row {
                        visible: App.library.tab === "playlists"
                        spacing: 4
                        bottomPadding: 20 + 16

                        Chip {
                            quiet: true
                            text: qsTr("Your playlists")
                            selected: App.library.playlistFilter === "manual"
                            onClicked: App.library.playlistFilter = "manual"
                        }

                        Chip {
                            quiet: true
                            text: qsTr("Auto mixes")
                            selected: App.library.playlistFilter === "auto"
                            onClicked: App.library.playlistFilter = "auto"
                        }
                    }

                    // .library-artist-toolbar: Top / Followed / All Artists,
                    // the search (230px and up, at most 360px), and how
                    // many; 10px apart, 18px above the cards.
                    RowLayout {
                        visible: cardTab.serverArtists
                        width: parent.width
                        spacing: 10

                        SegmentedControl {
                            readonly property var views: ["default", "followed", "all"]

                            choices: [qsTr("Top"), qsTr("Followed"), qsTr("All Artists")]
                            currentIndex: Math.max(0, views.indexOf(App.accountArtists.view))
                            onActivated: index => App.accountArtists.view = views[index]
                        }

                        SearchField {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 230
                            Layout.maximumWidth: 360
                            placeholderText: qsTr("Search all artists")
                            text: App.accountArtists.query
                            onTextChanged: App.accountArtists.query = text
                        }

                        // .section-pill
                        Rectangle {
                            visible: App.accountArtists.countText !== ""
                            implicitWidth: countLabel.implicitWidth + 2 * 9
                            implicitHeight: countLabel.implicitHeight + 2 * 3
                            radius: height / 2
                            color: Theme.neutral
                            border.width: 1
                            border.color: Theme.stroke

                            Text {
                                id: countLabel

                                anchors.centerIn: parent
                                text: App.accountArtists.countText.toUpperCase()
                                font: Theme.sectionPill
                                lineHeight: Theme.lineHeightSectionPill
                                lineHeightMode: Text.FixedHeight
                                color: Theme.fgSecondary
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Item {
                        visible: cardTab.serverArtists
                        width: 1
                        height: 18
                    }

                    // .empty-state (library.ts 427-431): the boxed sentence.
                    CompactEmptyState {
                        width: parent.width
                        compact: false
                        visible: cardTab.cards.count === 0 && !(cardTab.serverArtists && App.accountArtists.loading)
                        text: App.library.tab === "genres"
                            ? qsTr("No genre tags in your library yet. Genres appear as tagged tracks arrive.")
                            : cardTab.serverArtists ? qsTr("No artists match this view.")
                            : qsTr("Play or like some music to build your album collection.")
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
                    required property string badge
                    required property bool followed

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
                        badge: cell.badge

                        // .library-artist-followed: a 30px accent circle
                        // with a 13px heart, ringed 3px in the surface, on the
                        // round cover's edge at the lower right.
                        Rectangle {
                            visible: cardTab.serverArtists && cell.followed
                            x: parent.width - parent.width * 0.146 - 15
                            y: parent.width - parent.width * 0.146 - 15
                            z: 2
                            width: 30
                            height: 30
                            radius: 15
                            color: Theme.accentColor
                            border.width: 3
                            border.color: Theme.surface

                            Icon {
                                anchors.centerIn: parent
                                name: "heart-fill"
                                size: 13
                                color: Theme.accentInk
                            }

                            ToolTip.visible: followedHover.hovered
                            ToolTip.text: qsTr("Following")
                            ToolTip.delay: 500

                            HoverHandler {
                                id: followedHover
                            }
                        }

                        // .library-artist-follow-button: 5px under the
                        // caption, 12px 600, the accent (red to unfollow).
                        LinkButton {
                            visible: cardTab.serverArtists
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: parent.width + 12 + 22 + 18 + 5
                            z: 2
                            text: cell.followed ? qsTr("Unfollow") : qsTr("Follow")
                            labelFont: Theme.artistFollowLink
                            labelColor: cell.followed ? Theme.danger : Theme.accentColor
                            onClicked: App.accountArtists.toggleFollow(cell.index)
                        }

                        onClicked: cardTab.serverArtists ? App.accountArtists.openCard(cell.index)
                                                         : App.library.openCard(App.library.tab, cell.index)
                        onMenuRequested: (at, fromKeyboard) => {
                            if (cardTab.serverArtists || !App.library.prepareCardMenu(App.library.tab, cell.index))
                                return
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
                width: parent.width - (page.gutter + page.rightGutter)
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

    // A collection card's menu (library.ts openCardActionsMenu).
    ActionMenu {
        id: cardMenu

        items: App.library.cardMenu
        onTriggered: itemId => App.library.triggerCardMenu(itemId)
    }

    TrackMenus {
        id: menus
    }
}
