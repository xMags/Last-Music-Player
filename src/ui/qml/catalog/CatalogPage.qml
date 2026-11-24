pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The catalogue's album, playlist, chart and shelf pages (App.catalogPage;
// views/catalog.ts): the collection header, Save to library on albums and
// playlists, then the songs in the saved List or Grid layout, or the cards of
// a list of albums, playlists or artists. Charts and the long shelves load
// more as the end comes near. The page fills the panel and scrolls itself;
// Back returns to where it was.
Item {
    id: page

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane
    readonly property CatalogPageViewModel model: App.catalogPage
    property Flickable currentView: page.model.showsCards ? cardsView : songs.view
    // The save button a press that saves came from, until the page shows the
    // album saved; then the save motion starts there.
    property Item celebrateFrom: null

    Connections {
        target: page.model

        function onChanged() {
            if (page.celebrateFrom === null || page.model.saving)
                return
            if (page.model.saved)
                App.effects.collectionSaved(page.celebrateFrom)
            page.celebrateFrom = null
        }
    }

    ScrollMemory {
        target: page.currentView
    }

    Component {
        id: top

        Column {
            width: parent ? parent.width : 0
            spacing: 16

            // A shelf or an artist's section (renderCatalogShelf,
            // renderArtistSection): Back, then a plain view header with the
            // eyebrow, the title and what it holds, Play and Shuffle at its
            // foot on the right; no cover.
            Column {
                visible: page.model.kind === "shelf"
                width: parent.width

                // .catalog-back: margin 16px 0 22px.
                Item { width: 1; height: 16 }

                BackLink {
                    onClicked: App.navigator.back()
                }

                Item { width: 1; height: 22 }

                // .view-header: padding 18px 0 22px, the actions at the bottom.
                RowLayout {
                    width: parent.width
                    spacing: 16

                    Column {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignBottom
                        topPadding: 18
                        bottomPadding: 22

                        Text {
                            visible: page.model.eyebrow !== ""
                            bottomPadding: 4
                            text: page.model.eyebrow
                            font: Theme.eyebrow
                            color: Theme.fgSecondary
                        }

                        Text {
                            width: parent.width
                            text: page.model.title
                            font: Theme.viewTitle
                            color: Theme.fg
                            elide: Text.ElideRight
                        }

                        // .h-sub: 13px tertiary, 6px under.
                        Text {
                            visible: page.model.info !== ""
                            topPadding: 6
                            text: page.model.info
                            font: Theme.viewSubtitle
                            color: Theme.fgTertiary
                        }
                    }

                    Row {
                        Layout.alignment: Qt.AlignBottom
                        Layout.bottomMargin: 22
                        visible: page.model.tracks.count > 0
                        spacing: 10

                        PlayPill {
                            onClicked: page.model.play()
                        }

                        RoundAction {
                            iconName: "shuffle"
                            toolTip: qsTr("Shuffle")
                            Accessible.name: qsTr("Shuffle")
                            onClicked: page.model.shuffle()
                        }
                    }
                }

                // .track-view-toolbar: the List / Grid switch on the right.
                RowLayout {
                    visible: page.model.tracks.count > 0 || page.model.releaseList
                    width: parent.width

                    Item { Layout.fillWidth: true }

                    ViewSwitch {}
                }
            }

            CollectionHeader {
                visible: page.model.kind !== "shelf"
                width: parent.width
                backLabel: qsTr("Back")
                eyebrow: page.model.eyebrow
                title: page.model.title
                description: page.model.description
                byline: page.model.byline
                info: page.model.info
                canPlay: !page.model.showsCards && page.model.tracks.count > 0
                // "More options for <title>": Play next, Add to queue,
                // Download, then Share (catalog.ts openDetailMenu).
                showMenu: !page.model.showsCards
                onMenuClicked: at => {
                    detailItems.clear()
                    detailItems.addItem("next", qsTr("Play next"), "play-next")
                    detailItems.addItem("queue", qsTr("Add to queue"), "queue-add")
                    if (App.capabilities.downloadSongs && page.model.canShare)
                        detailItems.addItem("download", qsTr("Download"), "download", false, true)
                    if (App.share.available && page.model.canShare)
                        detailItems.addItem("share", qsTr("Share"), "share", false, true)
                    const place = mapToItem(page, at.x, at.y)
                    detailMenu.openAt(place.x - 20, place.y + 4)
                }
                showViewSwitch: !page.model.showsCards
                coverStyle: "image"
                artworkUrl: page.model.artworkUrl
                artworkId: page.model.artworkId
                artworkLabel: page.model.title.length > 0 ? page.model.title.charAt(0).toUpperCase() : ""
                onBackClicked: App.navigator.back()
                onPlayClicked: page.model.play()
                onShuffleClicked: page.model.shuffle()
                showShare: App.share.available && page.model.canShare
                onShareClicked: page.model.share()
                showDownload: App.capabilities.downloadSongs && page.model.canShare
                onDownloadClicked: page.model.download()
                showSave: page.model.canSave
                saved: page.model.saved
                saveBusy: page.model.saving
                onSaveClicked: button => {
                    page.celebrateFrom = page.model.saved ? null : button
                    page.model.toggleSave()
                }
            }

            Text {
                width: parent.width
                visible: page.model.finding
                text: qsTr("Finding the album…")
                font: Theme.stateText
                color: Theme.fgSecondary
            }

            SkeletonRows {
                width: parent.width
                visible: page.model.loading && page.model.error === ""
                rows: 8
            }

            ErrorState {
                width: parent.width
                visible: page.model.error !== ""
                title: page.model.error
                onActionClicked: page.model.retry()
            }
        }
    }

    TrackListView {
        id: songs

        anchors.fill: parent
        visible: !page.model.showsCards
        model: page.model.tracks
        grid: App.settings.trackView !== "list"
        sidePadding: page.gutter
        rightSidePadding: page.rightGutter
        header: top
        onActivated: row => page.model.playRow(row)
        onMenuRequested: (row, at, fromKeyboard) => {
            page.model.prepareSongMenu(row)
            menus.open(songs, at.x, at.y, fromKeyboard)
        }
    }

    Connections {
        target: songs.view
        enabled: !page.model.showsCards

        function onAtYEndChanged() {
            if (songs.view.atYEnd && page.model.pagingState === "idle")
                page.model.loadMore()
        }
    }

    // Releases in the List view: rows that open each release.
    readonly property bool releaseRows: page.model.releaseList && App.settings.trackView === "list"

    TrackListView {
        id: releaseTable

        anchors.fill: parent
        visible: page.releaseRows
        model: page.model.releases
        grid: false
        likes: false
        moreButtons: true
        albumHeading: qsTr("RELEASED")
        timeHeading: qsTr("SONGS")
        sidePadding: page.gutter
        rightSidePadding: page.rightGutter
        header: page.releaseRows ? top : null
        onActivated: row => page.model.openCard(row)
        onMenuRequested: (row, at, fromKeyboard) => page.openItemMenu(row, releaseTable.mapToItem(page, at.x, at.y), fromKeyboard)
    }

    // openCatalogItemMenu for a card or release row.
    function openItemMenu(row: int, at: point, fromKeyboard: bool) {
        if (!App.catalogItemMenu.prepare(page.model.cards, row))
            return
        itemMenu.openAt(at.x, at.y)
        itemMenu.keyboardActive = fromKeyboard
    }

    ActionMenu {
        id: itemMenu

        items: App.catalogItemMenu.menu
        onTriggered: itemId => App.catalogItemMenu.trigger(itemId)
    }

    Connections {
        target: releaseTable.view
        enabled: page.releaseRows

        function onAtYEndChanged() {
            if (releaseTable.view.atYEnd && page.model.pagingState === "idle")
                page.model.loadMore()
        }
    }

    GridView {
        id: cardsView

        anchors.fill: parent
        anchors.leftMargin: page.gutter
        anchors.rightMargin: page.rightGutter - 20
        visible: page.model.showsCards && !page.releaseRows
        clip: true
        // The header keeps the page's width, not the grid's wider one.
        header: Loader {
            width: cardsView.innerWidth
            sourceComponent: top
        }
        // .catalog-grid: repeat(auto-fill, minmax(150px, 1fr)), gap 28px 20px.
        // Each cell holds a card and the gaps after it, so the grid is 20px
        // wider than the page to take the last column's.
        readonly property real innerWidth: cardsView.width - 20
        readonly property int columns: Math.max(1, Math.floor(cardsView.width / (150 + 20)))
        readonly property real cardWidth: (cardsView.innerWidth - (cardsView.columns - 1) * 20) / cardsView.columns
        cellWidth: cardsView.cardWidth + 20
        cellHeight: cardsView.cardWidth + 12 + 22 + 18 + 28
        model: page.model.cards
        onAtYEndChanged: {
            if (cardsView.atYEnd && page.model.pagingState === "idle")
                page.model.loadMore()
        }

        AppScrollBar.vertical: AppScrollBar {}

        delegate: Item {
            id: cell

            required property int index
            required property string title
            required property string subtitle
            required property url artworkUrl
            required property string artworkId
            required property string artworkLabel
            required property bool round

            width: GridView.view.cellWidth
            height: GridView.view.cellHeight

            Card {
                width: cardsView.cardWidth
                title: cell.title
                subtitle: cell.subtitle
                artworkUrl: cell.artworkUrl
                artworkId: cell.artworkId
                artworkLabel: cell.artworkLabel
                round: cell.round
                hasMenu: true
                onClicked: App.catalogPage.openCard(cell.index)
                onMenuRequested: (at, fromKeyboard) => page.openItemMenu(cell.index, mapToItem(page, at.x, at.y), fromKeyboard)
            }
        }

        footer: Item {
            width: cardsView.innerWidth
            height: 64

            Text {
                anchors.centerIn: parent
                visible: App.catalogPage.pagingState === "loading" || App.catalogPage.pagingState === "failed"
                text: App.catalogPage.pagingState === "failed" ? qsTr("Could not load more. Scroll to try again.")
                                                               : qsTr("Loading more…")
                font: Theme.stateText
                color: Theme.fgSecondary
            }
        }

        WheelScroll { flickable: cardsView }
    }

    MenuItemListModel {
        id: detailItems
    }

    ActionMenu {
        id: detailMenu

        items: detailItems
        onTriggered: itemId => {
            if (itemId === "next")
                page.model.playNextAll()
            else if (itemId === "queue")
                page.model.addAllToQueue()
            else if (itemId === "download")
                page.model.download()
            else if (itemId === "share")
                page.model.share()
        }
    }

    TrackMenus {
        id: menus
    }
}
