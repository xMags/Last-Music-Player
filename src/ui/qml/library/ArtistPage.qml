pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// An artist from the local library (App.localArtist): the collection header
// with a round picture, Play and Shuffle; the top songs (five, then all
// with "Show all"); and a shelf of the albums the songs carry. It scrolls in
// the content panel like any other page.
Item {
    id: page

    readonly property LocalArtistViewModel model: App.localArtist
    readonly property real gutter: 32

    implicitHeight: content.implicitHeight

    Column {
        id: content

        x: page.gutter
        width: page.width - 2 * page.gutter
        bottomPadding: 40
        spacing: 0

        CollectionHeader {
            width: parent.width
            backLabel: qsTr("All artists")
            eyebrow: qsTr("ARTIST")
            title: page.model.name
            info: page.model.info
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
            showFollow: page.model.canFollow
            followed: page.model.followed
            followBusy: page.model.followBusy
            onFollowClicked: page.model.toggleFollow()
        }

        SectionHeader {
            width: parent.width
            title: qsTr("Top songs")
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
            clip: true

            Column {
                id: songs

                width: parent.width

                Repeater {
                    model: page.model.topSongs

                    TrackRow {
                        id: row

                        required property int index

                        width: songs.width
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

        Shelf {
            width: parent.width
            visible: page.model.albums.count > 0
            title: qsTr("Albums")
            model: page.model.albums

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
