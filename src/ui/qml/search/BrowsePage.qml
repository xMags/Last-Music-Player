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

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane

    implicitHeight: content.implicitHeight + 24

    Component.onCompleted: App.search.refreshLanding()

    Connections {
        target: App.search

        function onFocusRequested(atEnd) {
            if (atEnd)
                search.focusAtEnd()
            else
                search.focusField()
        }
    }

    Column {
        id: content

        x: page.gutter
        width: page.width - (page.gutter + page.rightGutter)
        spacing: 0

        // .search-bar: margin 16px auto 28px, at most 784px wide.
        Item { width: 1; height: 16 }

        SearchField {
            id: search

            width: Math.min(implicitWidth, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            placeholderText: qsTr("Search songs, artists, albums, links…")
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

            // .search-browse-landing: padding 10px 0 24px; no heading.
            Item { width: 1; height: 10 }

            // .search-minimum-hint: one character typed. 14px, the secondary
            // colour, margin 0 4px 14px.
            Text {
                x: 4
                visible: App.search.query.trim().length === 1
                bottomPadding: 14
                text: qsTr("Type at least 2 characters to search.")
                font: Theme.body
                color: Theme.fgSecondary
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
        // Searching with nothing on screen yet: rows of placeholders.
        SkeletonRows {
            width: parent.width
            visible: App.search.state === "searching"
            rows: 8
        }

        // .search-desktop-status (views.css 123-134): no results, or the
        // search failed. Padded 72px 20px; a 40px accent icon, the title
        // 16px below it at 17px, the text 6px below (14px/1.4, at most
        // 360px), Retry 16px below.
        Column {
            width: parent.width
            visible: App.search.state === "empty" || App.search.state === "error"
            topPadding: 72
            bottomPadding: 72

            readonly property bool failed: App.search.state === "error"

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                name: parent.failed ? "alert" : "browse"
                size: 40
                color: Theme.accentColor
            }

            Text {
                width: parent.width
                topPadding: 16
                horizontalAlignment: Text.AlignHCenter
                text: parent.failed ? qsTr("Search unavailable")
                                    : qsTr("No results for \"%1\"").arg(App.search.query.trim())
                font: Theme.stateTitle
                color: Theme.fg
                wrapMode: Text.Wrap
            }

            Text {
                width: Math.min(360, parent.width - 40)
                anchors.horizontalCenter: parent.horizontalCenter
                topPadding: 6
                horizontalAlignment: Text.AlignHCenter
                text: parent.failed ? App.search.errorText
                                    : qsTr("Try another song, artist, album, or music link.")
                font: Theme.stateText
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
            }

            Item {
                width: 1
                height: 16
                visible: parent.failed
            }

            AppButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: parent.failed
                variant: AppButton.Accent
                text: qsTr("Retry")
                onClicked: App.search.searchNow()
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
                    // Not drawn while it lies wholly outside the visible part of the page.
                    opacity: rowInView.inView ? 1 : 0

                    InViewWatch {
                        id: rowInView

                        target: row
                    }

                    SearchSectionHead {
                        width: parent.width
                        text: row.modelData.title
                        count: row.modelData.cards.count
                    }

                    Shelf {
                        width: parent.width
                        model: row.modelData.cards
                        cardWidth: 168

                        delegate: CollectionCard {
                            id: card

                            required property int index

                            // Search's card rows: 168px (views.css 2081-2082).
                            width: 168
                            onClicked: App.search.openCard(row.modelData.key, card.index)
                        }
                    }
                }
            }

            Column {
                id: allSongs

                width: parent.width
                visible: App.search.songs.count > 0
                opacity: allSongsInView.inView ? 1 : 0

                InViewWatch {
                    id: allSongsInView

                    target: allSongs
                }

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
