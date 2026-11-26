pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// One song in the list form (.track-row, views.css 173-251; common.ts
// trackRow): the number (a play glyph on hover, the bars while it plays),
// the 36px cover with title and artist, the album, the length, the heart and
// the "..." button. The playing row is tinted and its title takes the
// accent; an unavailable row is faded, says why, and does not play.
// Right click, the "..." button, the Menu key and Shift+F10 ask for the menu.
// A collection page adds a compact density (46 rather than 58px in the
// current app; here 41 rather than 55, the cover row less 12) and a
// selected look; `pressModifiers` says which of Ctrl and Shift were held
// when the click began.
T.AbstractButton {
    id: row

    required property int number
    required property string title
    required property string artist
    required property string album
    required property string durationText
    required property url artworkUrl
    required property string artworkId
    required property bool liked
    required property bool playing
    required property bool unavailable
    required property string unavailableReason
    required property string remoteUrl
    required property bool hasFile

    // .is-offline .track-row:not(.is-downloaded): opacity .45. A song with
    // its own file here plays anyway, whatever address it came from.
    readonly property bool offlineUnplayable: App.connectivity.offline && remoteUrl !== "" && !hasFile
        && App.offline.markRevision >= 0 && App.offline.markOf(remoteUrl).state !== "done"
    // The album column shows only when the row is wide enough for it.
    readonly property bool albumShown: width >= 560
    property bool selected: false
    property bool compact: false
    // A release row has no heart.
    property bool showLike: true
    // A release row puts "More" where the heart goes (releaseRow).
    property bool showMore: false
    readonly property int artSize: compact ? 28 : 36
    property int pressModifiers: Qt.NoModifier

    // The first and last row of a rounded box of rows round those corners.
    property bool roundTop: false
    property bool roundBottom: false

    signal likeRequested()
    signal menuRequested(point at, bool fromKeyboard)

    // .track-row: padding 8px, a 1px divider, and the taller of the cover and
    // the two text lines (.tr-title 20px + .tr-artist 18px): 55px.
    // Compact rows (an app-only density) stay at the 28px cover's 41px.
    implicitHeight: compact ? artSize + 2 * 6 + 1
                            : Math.max(artSize, Theme.lineHeightTrackTitle + Theme.lineHeightTrackArtist) + 2 * 8 + 1
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: qsTr("Play %1 by %2").arg(title).arg(artist)

    Keys.onMenuPressed: row.menuRequested(Qt.point(24, height / 2), true)
    Keys.onPressed: event => {
        if (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            row.menuRequested(Qt.point(24, row.height / 2), true)
            event.accepted = true
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => row.menuRequested(eventPoint.position, false)
    }

    // Sees the press before the button does, only to note the modifiers.
    TapHandler {
        id: leftPress

        acceptedButtons: Qt.LeftButton
        gesturePolicy: TapHandler.DragThreshold
        onPressedChanged: {
            if (pressed)
                row.pressModifiers = leftPress.point.modifiers
        }
    }

    HoverHandler {
        cursorShape: row.unavailable ? Qt.ForbiddenCursor : Qt.PointingHandCursor
    }

    // A song starting on screen: one band of light sweeps across its row
    // (motion.css 140-158), 760 ms.
    // The band is made for the first sweep, in the same moment, and kept.
    onPlayingChanged: {
        if (!row.playing || Motion.reduced || !row.visible)
            return
        band.active = true
        const sweep = band.item as RowSweep
        sweep.play()
    }


    background: Rectangle {
        // In a rounded box of rows, the first and last row follow its
        // corners (the box says which, from the row's index); text is never
        // put through a clipping layer, which would blur it.
        topLeftRadius: row.roundTop ? 12 : 0
        topRightRadius: row.roundTop ? 12 : 0
        bottomLeftRadius: row.roundBottom ? 12 : 0
        bottomRightRadius: row.roundBottom ? 12 : 0
        clip: true
        color: row.selected ? Theme.accentSoft2
             : row.playing ? Theme.accentSel
             : (row.hovered && !row.unavailable ? Theme.rowHover : "transparent")

        PressRipple {
            enabled: !row.unavailable
        }

        Loader {
            id: band

            anchors.fill: parent
            active: false
            sourceComponent: RowSweep {}
        }

        // border-bottom: 1px solid divider
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.divider
        }

        FocusRing {
            shown: row.visualFocus
            offset: -2 - Theme.focusRingWidth
        }
    }

    // The dimming sits on the inner row: the page cascade animates the
    // content item's own opacity, which would otherwise end the binding.
    contentItem: Item {
        implicitWidth: rowLayout.implicitWidth
        implicitHeight: rowLayout.implicitHeight

        RowLayout {
            id: rowLayout

            anchors.fill: parent
            opacity: row.unavailable ? 0.55 : row.offlineUnplayable ? 0.45 : 1
            spacing: 16

            // padding: 8px 20px
            Item { Layout.preferredWidth: 20 - 16 }

            TrackNumberCell {
                Layout.preferredWidth: 40
                Layout.fillHeight: true
                number: row.number
                hovered: row.hovered
                playing: row.playing
                unavailable: row.unavailable
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 12

                ArtTile {
                    Layout.preferredWidth: row.artSize
                    Layout.preferredHeight: row.artSize
                    radius: 6
                    source: row.artworkUrl
                    artworkId: row.artworkId
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 0

                    // .tr-title-line: the title, its download mark 6px after.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            // Rounded up: a width snapped down would elide a title that fits.
                            Layout.fillWidth: true
                            Layout.maximumWidth: Math.ceil(implicitWidth)
                            text: row.title
                            font: Theme.trackTitle
                            lineHeight: Theme.lineHeightTrackTitle
                            lineHeightMode: Text.FixedHeight
                            color: row.playing ? Theme.accentColor : Theme.fg
                            elide: Text.ElideRight
                        }

                        DownloadMark {
                            remoteUrl: row.remoteUrl
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: row.unavailable ? row.unavailableReason : row.artist
                        font: Theme.trackArtist
                        lineHeight: Theme.lineHeightTrackArtist
                        lineHeightMode: Text.FixedHeight
                        color: row.unavailable ? Theme.danger : Theme.fgTertiary
                        elide: Text.ElideRight
                    }
                }
            }

            Text {
                Layout.preferredWidth: 200
                visible: row.albumShown
                text: row.album
                font: Theme.trackAlbum
                color: Theme.fgSecondary
                elide: Text.ElideRight
            }

            Text {
                // The 60px time column (grid-template-columns: 40px 1fr 200px 60px 40px).
                Layout.preferredWidth: 60
                horizontalAlignment: Text.AlignRight
                text: row.unavailable ? "-" : row.durationText
                font: Theme.trackNumber
                color: Theme.fgTertiary
            }

            // The 40px last column holds only the 28px heart, at its start.
            Item {
                Layout.preferredWidth: 40
                implicitHeight: likeButton.implicitHeight

                Loader {
                    anchors.verticalCenter: parent.verticalCenter
                    active: row.showMore
                    sourceComponent: IconButton {
                        id: moreButton

                        iconName: "more"
                        shape: IconButton.Square
                        iconSize: 14
                        implicitWidth: 28
                        implicitHeight: 28
                        Accessible.name: qsTr("More options for %1").arg(row.title)
                        ToolTip.visible: moreButton.hovered
                        ToolTip.text: qsTr("More")
                        ToolTip.delay: 500
                        onClicked: row.menuRequested(moreButton.mapToItem(row, moreButton.width / 2, moreButton.height), false)
                    }
                }

                IconButton {
                    id: likeButton

                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.showLike

                    iconName: row.liked ? "heart-fill" : "heart"
                    active: row.liked
                    shape: IconButton.Square
                    iconSize: 14
                    implicitWidth: 28
                    implicitHeight: 28
                    Accessible.name: row.liked ? qsTr("Remove from Liked Songs") : qsTr("Add to Liked Songs")
                    onClicked: {
                        if (!row.liked)
                            App.effects.heartLiked(likeButton)
                        row.likeRequested()
                    }
                }
            }

            Item { Layout.preferredWidth: 20 - 16 }
        }
    }

    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: row.contentItem
    }
}
