import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The top of the Up Next tab (views/queue.ts 155-161; styles.css 1247-1269):
// the "Now playing" label, the cover as wide as the rail with its breathing
// glow behind it (16px radius, card shadow), then the title and, in the
// accent colour, the artist.
ColumnLayout {
    id: header

    spacing: 0

    RailSectionLabel {
        Layout.fillWidth: true
        Layout.bottomMargin: 16
        text: qsTr("NOW PLAYING")
    }

    // .np-art-stage: margin-bottom 18px; the cover is square.
    Item {
        id: stage

        Layout.fillWidth: true
        Layout.preferredHeight: width
        Layout.bottomMargin: 18

        // inset: -2% -14% -24%
        QueueArtGlow {
            x: -0.14 * stage.width
            y: -0.02 * stage.height
            width: 1.28 * stage.width
            height: 1.26 * stage.height
            playing: App.playerBar.playing
        }

        ArtTile {
            anchors.fill: parent
            radius: Theme.radius2xl
            shadow: true
            shadowLayers: Theme.shadowCard
            big: true
            source: App.playerBar.artworkUrl
            artworkId: App.playerBar.artworkId
        }
    }

    AppText {
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        text: App.playerBar.title
        font: Theme.nowPlayingTitle
        lineHeight: Theme.lineHeightNowPlayingTitle
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
        elide: Text.ElideRight
    }

    AppText {
        Layout.fillWidth: true
        Layout.bottomMargin: 22
        text: App.playerBar.artist
        font: Theme.nowPlayingArtist
        lineHeight: Theme.lineHeightNowPlayingArtist
        lineHeightMode: Text.FixedHeight
        color: Theme.accentColor
        elide: Text.ElideRight
    }
}
