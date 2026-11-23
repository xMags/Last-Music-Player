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

    readonly property real gutter: 32
    readonly property CatalogPageViewModel model: App.catalogPage
    property Flickable currentView: page.model.showsCards ? cardsView : songs.view

    ScrollMemory {
        target: page.currentView
    }

    Component {
        id: top

        Column {
            width: parent ? parent.width : 0
            spacing: 16

            CollectionHeader {
                width: parent.width
                backLabel: qsTr("Back")
                eyebrow: page.model.eyebrow
                title: page.model.title
                description: page.model.description
                byline: page.model.byline
                info: page.model.info
                canPlay: !page.model.showsCards && page.model.tracks.count > 0
                showMenu: false
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
            }

            AppButton {
                id: saveButton

                // Set by a press that saves, until the page shows it saved.
                property bool celebrating: false

                visible: page.model.canSave
                enabled: !page.model.saving
                iconName: page.model.saved ? "check" : "playlist-add"
                text: page.model.saving ? qsTr("Saving…")
                    : page.model.saved ? qsTr("Saved to library") : qsTr("Save to library")
                onClicked: {
                    saveButton.celebrating = !page.model.saved
                    page.model.toggleSave()
                }

                Connections {
                    target: page.model

                    function onChanged() {
                        if (!saveButton.celebrating || page.model.saving)
                            return
                        if (page.model.saved)
                            App.effects.collectionSaved(saveButton)
                        saveButton.celebrating = false
                    }
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

    GridView {
        id: cardsView

        anchors.fill: parent
        anchors.leftMargin: page.gutter
        anchors.rightMargin: page.gutter
        visible: page.model.showsCards
        clip: true
        header: top
        readonly property int columns: Math.max(2, Math.floor(cardsView.width / 176))
        cellWidth: cardsView.width / cardsView.columns
        cellHeight: cardsView.cellWidth + 56
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
                x: 8
                width: cell.width - 16
                title: cell.title
                subtitle: cell.subtitle
                artworkUrl: cell.artworkUrl
                artworkId: cell.artworkId
                artworkLabel: cell.artworkLabel
                round: cell.round
                hasMenu: false
                onClicked: App.catalogPage.openCard(cell.index)
            }
        }

        footer: Item {
            width: cardsView.width
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

    TrackMenus {
        id: menus
    }
}
