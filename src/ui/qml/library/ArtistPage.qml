pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// An artist page with two sources: the local library (App.localArtist) and
// the catalogue (App.catalogArtist). The collection header with a round
// picture, Play, Shuffle and Follow; the top songs (five, then all with
// "Show all"); then the library's album shelf, or the catalogue's sections
// (latest release, albums, singles and EPs, similar artists) with "See all".
// It scrolls in the content panel like any other page.
Item {
    id: page

    // App.localArtist or App.catalogArtist; their properties are named alike.
    property var model: App.localArtist
    readonly property bool catalogue: page.model === App.catalogArtist
    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane

    implicitHeight: content.implicitHeight

    Column {
        id: content

        x: page.gutter
        width: page.width - (page.gutter + page.rightGutter)
        bottomPadding: 40
        spacing: 0

        CatalogArtistHero {
            width: parent.width
            visible: page.catalogue
            model: page.catalogue ? page.model : App.catalogArtist
            onBackClicked: App.navigator.back()
        }

        CollectionHeader {
            width: parent.width
            visible: !page.catalogue
            backLabel: page.catalogue ? qsTr("Back") : qsTr("All artists")
            eyebrow: qsTr("ARTIST")
            title: page.model.name
            info: page.model.info
            description: page.catalogue ? page.model.description : ""
            canPlay: page.model.topSongs.count > 0
            showMenu: false
            showViewSwitch: false
            roundCover: true
            coverStyle: "image"
            artworkUrl: page.model.artworkUrl
            artworkId: page.model.artworkId
            artworkLabel: page.model.artworkLabel
            onBackClicked: App.navigator.back()
            onPlayClicked: page.model.play()
            onShuffleClicked: page.model.shuffle()
            showShare: page.catalogue && App.share.available && page.model.canShare
            onShareClicked: page.model.share()
            showFollow: page.model.canFollow
            followed: page.model.followed
            followBusy: page.model.followBusy
            onFollowClicked: page.model.toggleFollow()
        }

        SkeletonRows {
            width: parent.width
            visible: page.catalogue && page.model.loading
            rows: 5
        }

        ErrorState {
            width: parent.width
            visible: page.catalogue && page.model.error !== ""
            title: page.catalogue ? page.model.error : ""
            onActionClicked: page.model.retry()
        }

        SectionHeader {
            width: parent.width
            visible: page.model.topSongs.count > 0
            title: qsTr("Top songs")
            pill: page.catalogue ? qsTr("Trending") : ""
            linkText: page.model.hasMore ? (page.model.showingAll ? qsTr("Show less") : qsTr("Show all")) : ""
            onLinkClicked: page.model.showingAll = !page.model.showingAll
        }

        Item { width: 1; height: 8 }

        Rectangle {
            width: parent.width
            height: songs.implicitHeight
            radius: 12
            color: Theme.surface
            border.width: 1
            border.color: Theme.stroke

            // The first and last rows round their own corners (TrackRow).
            Column {
                id: songs

                width: parent.width

                Repeater {
                    model: page.model.topSongs

                    TrackRow {
                        id: row

                        required property int index

                        width: songs.width
                        roundTop: row.index === 0
                        roundBottom: row.index === page.model.topSongs.count - 1
                        onClicked: page.model.playSong(row.index)
                        onLikeRequested: page.model.likeSong(row.index)
                        onMenuRequested: (at, fromKeyboard) => {
                            page.model.prepareSongMenu(row.index)
                            menus.open(row, at.x, at.y, fromKeyboard)
                        }
                    }
                }
            }
        }

        Item { width: 1; height: 32 }

        // The catalogue's sections.
        Repeater {
            model: page.catalogue ? page.model.sections : []

            Column {
                id: section

                required property var modelData

                width: content.width

                Shelf {
                    width: parent.width
                    title: section.modelData.title
                    pill: section.modelData.pill
                    linkText: section.modelData.link ? qsTr("View more ›") : ""
                    model: App.catalogArtist.sectionCards(section.modelData.key)
                    onLinkClicked: App.catalogArtist.seeAllSection(section.modelData.key)

                    delegate: Card {
                        id: sectionCard

                        required property int index
                        required title
                        required subtitle
                        required artworkUrl
                        required artworkId
                        required artworkLabel
                        required round

                        width: 156
                        hasMenu: false
                        onClicked: App.catalogArtist.openSectionCard(section.modelData.key, sectionCard.index)
                    }
                }

                Item { width: 1; height: 32 }
            }
        }

        Shelf {
            width: parent.width
            visible: !page.catalogue && page.model.albums.count > 0
            title: qsTr("Albums")
            model: page.catalogue ? null : page.model.albums

            delegate: CollectionCard {
                required property int index

                width: 156
                onClicked: page.model.openAlbum(index)
            }
        }

    }

    TrackMenus {
        id: menus
    }
}
