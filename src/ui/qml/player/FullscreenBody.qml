import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The middle of the full-screen player (.fs-body, styles.css 2121-2178): the
// large cover with its deep shadow, the title, artist and album line, the
// seek row, and the controls (like at the start, the transport in the
// middle, share at the end), in a centred column 6px apart inside 24px of
// padding. The owner sets the width and the cover size.
Item {
    id: body

    required property real artSize
    // Moves keyboard focus to the play button (when the player opens).
    function focusPlay() {
        play.forceActiveFocus(Qt.OtherFocusReason)
    }

    implicitHeight: column.implicitHeight + 2 * 24

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.margins: 24
        spacing: 6

        ArtTile {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: body.artSize
            Layout.preferredHeight: body.artSize
            Layout.bottomMargin: 22
            big: true
            radius: Theme.fullscreenArtRadius
            shadow: true
            shadowLayers: Theme.shadowFullscreenArt
            source: App.playerBar.artworkUrl
            artworkId: App.playerBar.artworkId
            label: App.playerBar.artworkLabel
        }

        Text {
            Layout.fillWidth: true
            text: App.playerBar.displayTitle
            font: Theme.fullScreenTitle
            lineHeight: Theme.lineHeightFullScreenTitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fullscreenText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        Text {
            Layout.fillWidth: true
            text: App.playerBar.displayArtist
            font: Theme.fullscreenArtist
            lineHeight: Theme.lineHeightFullscreenArtist
            lineHeightMode: Text.FixedHeight
            color: Theme.fullscreenArtistText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            visible: text !== ""
            text: App.fullscreen.albumLine
            font: Theme.fullscreenMeta
            color: Theme.fullscreenMetaText
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        // .fs-seek: gap 12px, 18px below the text.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            spacing: 12

            Text {
                text: App.playerBar.elapsedText
                font: Theme.fullscreenTime
                color: Theme.fullscreenTimeText
            }

            AppSlider {
                id: seek

                Layout.fillWidth: true
                fillColor: Theme.fullscreenSeekFill
                trackColor: Theme.fullscreenSeekTrack
                trackThickness: 5
                engagedTrackThickness: 7
                sourceValue: App.playerBar.progress
                bubbleText: App.playerBar.seekLabelAt(seek.hoverRatio)
                onMoved: App.playerBar.seekToRatio(seek.value)
            }

            Text {
                text: App.playerBar.totalText
                font: Theme.fullscreenTime
                color: Theme.fullscreenTimeText
            }
        }

        // .fs-controls: 16px below the seek row.
        Item {
            Layout.fillWidth: true
            Layout.topMargin: 16
            implicitHeight: transport.implicitHeight

            FullscreenButton {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                iconName: App.playerBar.liked ? "heart-fill" : "heart"
                iconSize: 22
                active: App.playerBar.liked
                onClicked: App.playerBar.toggleLike()
            }

            // .fs-transport: 16px apart.
            Row {
                id: transport

                anchors.centerIn: parent
                spacing: 16

                FullscreenButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "shuffle"
                    active: App.playerBar.shuffle
                    onClicked: App.playerBar.toggleShuffle()
                }

                FullscreenButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip-back"
                    iconSize: 26
                    skip: true
                    onClicked: App.playerBar.previous()
                }

                FullscreenPlayButton {
                    id: play

                    anchors.verticalCenter: parent.verticalCenter
                    playing: App.playerBar.playing
                    onClicked: App.playerBar.togglePlay()
                }

                FullscreenButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "skip-fwd"
                    iconSize: 26
                    skip: true
                    onClicked: App.playerBar.next()
                }

                FullscreenButton {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "repeat"
                    active: App.playerBar.repeatMode !== 0
                    badge: App.playerBar.repeatMode === 2 ? "1" : ""
                    onClicked: App.playerBar.cycleRepeat()
                }
            }

            FullscreenButton {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                iconName: "share"
                onClicked: App.playerBar.share()
            }
        }
    }
}
