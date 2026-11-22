import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The song side of the player bar (.pb-track, styles.css 1359-1384): the
// 52px cover, title and artist, and the like and share buttons, 16px apart.
RowLayout {
    id: block

    required property bool idle

    spacing: 16

    Item {
        Layout.preferredWidth: 52
        Layout.preferredHeight: 52

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
            label: App.playerBar.title.trim().charAt(0)
        }
    }

    Column {
        Layout.fillWidth: true
        Layout.minimumWidth: 0

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
