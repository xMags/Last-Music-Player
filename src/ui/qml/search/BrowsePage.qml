pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Browse (views/search.ts): the search bar at the top with its Ctrl+K hint.
// With no query: the recent searches as chips (the current app's) and the
// category tiles, four across. While searching with nothing to show yet,
// "Searching"; with no hits, why. With hits: the top result card and the
// first four songs side by side, the artists, albums and playlists as card
// rows, and every song in the saved List / Grid layout with a sort. The
// page scrolls in the content panel.
Item {
    id: page

    readonly property real gutter: 32

    implicitHeight: content.implicitHeight + 48

    Component.onCompleted: App.search.refreshLanding()

    Connections {
        target: App.search

        function onFocusRequested() {
            search.focusField()
        }
    }

    Column {
        id: content

        x: page.gutter
        y: 24
        width: page.width - 2 * page.gutter
        spacing: 0

        SearchField {
            id: search

            width: parent.width
            placeholderText: qsTr("Search songs, artists, albums…")
            shortcutHint: qsTr("Ctrl + K")
            text: App.search.query
            onQueryEdited: query => App.search.query = query
            onSubmitted: App.search.searchNow()
        }

        Item { width: 1; height: 28 }

        // The landing: recent searches, then the category tiles.
        Column {
            width: parent.width
            visible: App.search.state === "landing"
            spacing: 0

            SearchSectionHead {
                width: parent.width
                visible: App.search.recentSearches.length > 0
                text: qsTr("RECENT SEARCHES")
                linkText: qsTr("Clear")
                onLinkClicked: App.search.clearRecents()
            }

            Flow {
                width: parent.width
                visible: App.search.recentSearches.length > 0
                spacing: 8
                bottomPadding: 26

                Repeater {
                    model: App.search.recentSearches

                    Chip {
                        id: chip

                        required property int index
                        required property string modelData

                        text: modelData
                        onClicked: App.search.useRecent(chip.index)

                        TapHandler {
                            acceptedButtons: Qt.MiddleButton
                            onTapped: App.search.removeRecent(chip.index)
                        }
                    }
                }
            }

            SearchSectionHead {
                width: parent.width
                text: qsTr("BROWSE")
            }

            Grid {
                id: categoryGrid

                width: parent.width
                columns: width < 660 ? 2 : 4
                spacing: 12

                Repeater {
                    model: App.search.categories

                    CategoryTile {
                        id: tile

                        required property int index

                        width: (content.width - 12 * (categoryGrid.columns - 1)) / categoryGrid.columns
                        onClicked: App.search.openCategory(tile.index)
                    }
                }
            }
        }

        // Waiting for the first answer, or nothing found.
        Column {
            width: parent.width
            visible: App.search.state === "searching" || App.search.state === "empty"
            topPadding: 48
            spacing: 10

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: "browse"
                size: 40
                color: Theme.fgTertiary
                visible: App.search.state === "empty"
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: App.search.state === "searching" ? qsTr("Searching…")
                    : qsTr("No results for “%1”").arg(App.search.query.trim())
                font: Theme.stateTitle
                color: Theme.fg
            }

            Text {
                width: parent.width
                visible: App.search.state === "empty"
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Try another song, artist or album.")
                font: Theme.stateText
                color: Theme.fgSecondary
            }
        }

        // The results.
        Column {
            width: parent.width
            visible: App.search.state === "results"
            spacing: 26

            // grid-template-columns: minmax(260px, 2fr) minmax(0, 3fr); gap 24px.
            Row {
                id: topRow

                readonly property bool hasTop: App.search.topKind !== ""
                readonly property real topWidth: hasTop ? Math.max(260, (width - 24) * 2 / 5) : 0

                width: parent.width
                spacing: hasTop ? 24 : 0
                visible: App.search.topSongs.count > 0 || hasTop

                Column {
                    width: topRow.topWidth
                    visible: topRow.hasTop

                    SearchSectionHead {
                        width: parent.width
                        text: qsTr("TOP RESULT")
                    }

                    SearchTopCard {
                        width: parent.width
                        height: Math.max(188, topSongs.height)
                        onClicked: App.search.openTop()
                    }
                }

                Column {
                    width: topRow.width - topRow.topWidth - topRow.spacing

                    SearchSectionHead {
                        width: parent.width
                        text: qsTr("SONGS")
                    }

                    SongTable {
                        id: topSongs

                        width: parent.width
                        model: App.search.topSongs
                        list: "top"
                        menu: menus
                    }
                }
            }

            Repeater {
                model: [
                    { key: "artists", title: qsTr("ARTISTS"), cards: App.search.artists },
                    { key: "albums", title: qsTr("ALBUMS"), cards: App.search.albums },
                    { key: "playlists", title: qsTr("PLAYLISTS"), cards: App.search.playlists }
                ]

                Column {
                    id: row

                    required property var modelData

                    width: content.width
                    visible: row.modelData.cards.count > 0

                    SearchSectionHead {
                        width: parent.width
                        text: row.modelData.title
                        count: row.modelData.cards.count
                    }

                    Shelf {
                        width: parent.width
                        model: row.modelData.cards

                        delegate: CollectionCard {
                            id: card

                            required property int index

                            width: 156
                            onClicked: App.search.openCard(row.modelData.key, card.index)
                        }
                    }
                }
            }

            Column {
                width: parent.width
                visible: App.search.songs.count > 0

                SearchSectionHead {
                    width: parent.width
                    text: qsTr("ALL SONGS")
                    count: App.search.songs.count
                }

                RowLayout {
                    width: parent.width

                    SegmentedControl {
                        compact: true
                        choices: [qsTr("List"), qsTr("Grid")]
                        currentIndex: App.settings.trackView === "list" ? 0 : 1
                        onActivated: index => App.settings.trackView = index === 0 ? "list" : "grid"
                    }

                    Item { Layout.fillWidth: true }

                    AppSelect {
                        model: App.search.songSorts
                        currentIndex: App.search.songSort
                        onActivated: index => App.search.songSort = index
                    }
                }

                Item { width: 1; height: 14 }

                SongTable {
                    width: parent.width
                    visible: App.settings.trackView === "list"
                    model: App.search.songs
                    list: "all"
                    menu: menus
                }

                Flow {
                    width: parent.width
                    visible: App.settings.trackView !== "list"
                    spacing: 20

                    Repeater {
                        model: App.settings.trackView !== "list" ? App.search.songs : null

                        TrackTile {
                            id: songTile

                            required property int index

                            width: 160
                            onClicked: App.search.playSong("all", songTile.index)
                            onMenuRequested: (at, fromKeyboard) => {
                                App.search.prepareSongMenu("all", songTile.index)
                                menus.open(songTile, at.x, at.y, fromKeyboard)
                            }
                        }
                    }
                }
            }
        }
    }

    // A short song table (at most sixty rows): the rounded surface with rows.
    component SongTable: Rectangle {
        id: table

        property alias model: rows.model
        property string list: ""
        // The page's song menu.
        required property TrackMenus menu

        height: column.implicitHeight
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
        clip: true

        Column {
            id: column

            width: parent.width

            Repeater {
                id: rows

                TrackRow {
                    id: songRow

                    required property int index

                    width: column.width
                    onClicked: App.search.playSong(table.list, songRow.index)
                    onLikeRequested: App.search.likeSong(table.list, songRow.index)
                    onMenuRequested: (at, fromKeyboard) => {
                        App.search.prepareSongMenu(table.list, songRow.index)
                        table.menu.open(songRow, at.x, at.y, fromKeyboard)
                    }
                }
            }
        }
    }

    TrackMenus {
        id: menus
    }
}
