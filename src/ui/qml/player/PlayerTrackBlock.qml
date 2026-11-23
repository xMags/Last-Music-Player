import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The song side of the player bar (.pb-track, styles.css 1359-1384): the
// 52px cover, title and artist, and the like and share buttons, 16px apart.
//
// A new song (song-change-motion.ts): the old title and artist, copied over
// the new ones, slide up and fade out (220ms, ease-in) while the new ones
// rise 10px into place after 90ms (320ms, water), and the cover settles in
// with a small zoom (380ms, water). Nothing moves under reduced motion.
RowLayout {
    id: block

    required property bool idle
    // 0 to 1 over the cover's settle; 1 at rest.
    property real coverProgress: 1

    spacing: 16

    Connections {
        target: App.playerBar

        function onSongChanged() {
            if (!Motion.reduced)
                songChange.restart()
        }
    }

    ParallelAnimation {
        id: songChange

        NumberAnimation { target: ghost; property: "opacity"; from: 1; to: 0; duration: Motion.songChangeOut; easing: Motion.easeIn }
        NumberAnimation { target: ghostShift; property: "y"; from: 0; to: -Motion.songChangeShift; duration: Motion.songChangeOut; easing: Motion.easeIn }

        SequentialAnimation {
            PropertyAction { target: meta; property: "opacity"; value: 0 }
            PropertyAction { target: metaShift; property: "y"; value: Motion.songChangeShift }
            PauseAnimation { duration: Motion.songChangeInDelay }
            ParallelAnimation {
                NumberAnimation { target: meta; property: "opacity"; to: 1; duration: Motion.songChangeIn; easing: Motion.water }
                NumberAnimation { target: metaShift; property: "y"; to: 0; duration: Motion.songChangeIn; easing: Motion.water }
            }
        }

        NumberAnimation { target: block; property: "coverProgress"; from: 0; to: 1; duration: Motion.songChangeCover; easing: Motion.water }
    }

    // Hidden by the "Album art on player bar" setting; the text moves over.
    Item {
        Layout.preferredWidth: 52
        Layout.preferredHeight: 52
        visible: App.settings.showAlbumArt
        scale: Motion.songChangeCoverScale(block.coverProgress)

        IdleArt {
            anchors.fill: parent
            visible: block.idle
            radius: Theme.radiusMd
        }

        ArtTile {
            anchors.fill: parent
            visible: !block.idle
            radius: Theme.radiusMd
            source: App.playerBar.artworkUrl
            artworkId: App.playerBar.artworkId
            label: App.playerBar.artworkLabel
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: meta.implicitHeight

        Column {
            id: meta

            width: parent.width
            transform: Translate { id: metaShift }

            Text {
                width: parent.width
                text: App.playerBar.displayTitle
                font: Theme.playerBarTitle
                lineHeight: Theme.lineHeightPlayerBarTitle
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                topPadding: 2
                text: App.playerBar.displayArtist
                font: Theme.playerBarArtist
                lineHeight: Theme.lineHeightPlayerBarArtist
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                elide: Text.ElideRight
            }
        }

        // The previous song's text, only while it slides away.
        Column {
            id: ghost

            width: parent.width
            visible: songChange.running
            transform: Translate { id: ghostShift }

            Text {
                width: parent.width
                text: App.playerBar.previousTitle
                font: Theme.playerBarTitle
                lineHeight: Theme.lineHeightPlayerBarTitle
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                topPadding: 2
                text: App.playerBar.previousArtist
                font: Theme.playerBarArtist
                lineHeight: Theme.lineHeightPlayerBarArtist
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                elide: Text.ElideRight
            }
        }
    }

    // Kept in place but hidden while idle, so nothing moves when a song loads.
    IconButton {
        opacity: block.idle ? 0 : 1
        enabled: !block.idle
        iconName: App.playerBar.liked ? "heart-fill" : "heart"
        active: App.playerBar.liked
        onClicked: App.playerBar.toggleLike()
    }

    IconButton {
        opacity: block.idle ? 0 : 1
        enabled: !block.idle
        iconName: "share"
        onClicked: App.playerBar.share()
    }
}
