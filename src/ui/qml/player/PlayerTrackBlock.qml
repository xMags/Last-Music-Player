import QtQuick
import QtQuick.Controls
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

            // While another of the account's devices plays.
            Text {
                width: parent.width
                visible: App.playerBar.remote
                topPadding: 2
                text: qsTr("Playing on %1").arg(App.playerBar.playingOn)
                font: Theme.trackArtist
                color: Theme.accentColor
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
        Accessible.name: App.playerBar.liked ? qsTr("Remove from Liked Songs") : qsTr("Add to Liked Songs")
        id: likeButton

        // Likes are the account's, so another device's song is liked here too.
        opacity: block.idle ? 0 : 1
        enabled: !block.idle
        iconName: App.playerBar.liked ? "heart-fill" : "heart"
        active: App.playerBar.liked
        ToolTip.visible: hovered
        ToolTip.text: App.playerBar.liked ? qsTr("Remove from Liked Songs") : qsTr("Add to Liked Songs")
        ToolTip.delay: 500
        onClicked: {
            if (!App.playerBar.liked)
                App.effects.heartLiked(likeButton)
            App.playerBar.toggleLike()
        }
    }

    IconButton {
        visible: App.share.available
        opacity: block.idle ? 0 : 1
        enabled: !block.idle && !App.playerBar.remote
        iconName: "share"
        Accessible.name: qsTr("Share")
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Share")
        ToolTip.delay: 500
        onClicked: App.playerBar.share()
    }

    // Download, or where the song's download stands: the same mark the
    // lists show (downloaded, on its way, failed). A song already listed is
    // not asked for again; a failed one is tried again.
    IconButton {
        id: downloadButton

        // By the song's download key, as the service matches it.
        readonly property var mark: App.offline.markRevision >= 0
                                    ? App.offline.playingMark(App.playerBar.downloadAddress) : ({})
        readonly property string markState: mark.state !== undefined ? mark.state : ""
        readonly property bool listed: markState !== "" && markState !== "failed"

        visible: App.capabilities.downloadSongs
        opacity: block.idle ? 0 : 1
        // Another device's song can be downloaded to this PC as well.
        enabled: !block.idle
        iconName: markState === "" ? "download" : ""
        Accessible.name: markState === "" ? qsTr("Download this song") : mark.label
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        ToolTip.delay: 500
        onClicked: {
            if (!downloadButton.listed)
                App.offline.downloadPlaying()
        }

        DownloadMark {
            anchors.centerIn: parent
            given: downloadButton.mark
            showsLabel: false
        }
    }
}
