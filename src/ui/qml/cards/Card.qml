pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A card of a grid or a shelf (.lib-card and .album-card, views.css
// 265-300 and styles.css 564-624): a square cover with large corners (a
// circle for an artist), the name and a line under it. On hover the cover's
// shadow deepens and the picture zooms a touch inside its frame, and the
// round "..." button shows in its corner. A cover can be replaced by any
// item (a collage, a gradient tile) through `art`.
T.AbstractButton {
    id: card

    property string title: ""
    property string subtitle: ""
    // .album-date: a third line, 2px under, 11px tertiary (Listen Again).
    property string dateText: ""
    // A song card's address, for the download mark beside its name.
    property string remoteUrl: ""
    property url artworkUrl
    property string artworkId: ""
    // An artist: the cover is a circle and the text is centred.
    property bool round: false
    // Library's artist cards centre their text (.library-artist-card); a
    // catalogue artist card only rounds its cover.
    property bool centred: round
    // Shows the "..." button on hover.
    property bool hasMenu: true
    // Replaces the cover picture when set.
    property Component art: null

    signal menuRequested(point at, bool fromKeyboard)

    implicitWidth: 160
    implicitHeight: width + 12 + nameText.implicitHeight + metaText.implicitHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: subtitle !== "" ? qsTr("%1, %2").arg(title).arg(subtitle) : title

    // A card without a menu ignores the right button and the Menu key too.
    Keys.onMenuPressed: {
        if (card.hasMenu)
            card.menuRequested(Qt.point(width / 2, width / 2), true)
    }

    onHoveredChanged: {
        if (card.hovered && !card.round)
            sheen.active = true
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: card.hasMenu
        onTapped: eventPoint => card.menuRequested(eventPoint.position, false)
    }

    HoverHandler {
        id: pointer

        cursorShape: Qt.PointingHandCursor
    }

    // Where the cover was, in case this opens a page whose header shows it
    // (the cover flight). Noted before the owner's handler navigates.
    onClicked: {
        if (card.art === null && card.artworkUrl.toString() !== "")
            App.coverFlight.notePress(cover.mapToItem(null, 0, 0, cover.width, cover.height), card.artworkUrl,
                                      card.artworkId !== "" ? card.artworkId : card.title,
                                      card.round)
    }

    contentItem: Item {
        Item {
            id: cover

            width: card.width
            height: card.width

            BoxShadow {
                anchors.fill: parent
                radius: card.round ? width / 2 : Theme.radiusXl
                layers: card.hovered ? Theme.shadowCardHover : Theme.shadowCard
            }

            // The picture zooms inside a frame cut to the card's corners. The
            // clip is a layer only while zoomed, so the art (which may carry
            // a glyph) is drawn directly, and sharp, the rest of the time.
            RoundedClip {
                anchors.fill: parent
                radius: zoom.scale > 1 ? (card.round ? width / 2 : Theme.radiusXl) : 0

                Item {
                    id: zoom

                    anchors.fill: parent
                    scale: card.hovered && !Motion.reduced ? 1.045 : 1
                    transformOrigin: Item.Center

                    Behavior on scale {
                        enabled: !Motion.reduced
                        NumberAnimation { duration: Motion.cardZoom; easing: Motion.standard }
                    }

                    ArtTile {
                        anchors.fill: parent
                        visible: card.art === null
                        radius: card.round ? width / 2 : Theme.radiusXl
                        big: true
                        source: card.artworkUrl
                        artworkId: card.artworkId !== "" ? card.artworkId : card.title
                    }

                    Loader {
                        anchors.fill: parent
                        active: card.art !== null
                        sourceComponent: card.art
                    }

                    // The hover light, made on the first hover of a square
                    // cover and kept.
                    Loader {
                        id: sheen

                        anchors.fill: parent
                        active: false
                        sourceComponent: CardSheen {
                            pointerAt: pointer.point.position
                            shown: card.hovered && !card.round
                        }
                        onLoaded: (sheen.item as CardSheen).lit = true
                    }
                }
            }

            // No "…" on the cover: on the desktop a card's actions open
            // with a right-click or the Menu key (.card-menu-button is
            // display: none, views.css 2085).

            // A cover is a picture or a gradient: light water on it.
            PressRipple {
                radius: card.round ? width / 2 : Theme.radiusXl
                surfaceColor: "black"
            }

            FocusRing {
                shown: card.visualFocus
                controlRadius: card.round ? width / 2 : Theme.radiusXl
            }
        }

        AppText {
            id: nameText

            y: cover.height + 12
            width: card.width - (cardMark.visible ? cardMark.width + 6 : 0)
            horizontalAlignment: card.centred ? Text.AlignHCenter : Text.AlignLeft
            text: card.title
            font: Theme.cardTitle
            lineHeight: Theme.lineHeightCardTitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
            elide: Text.ElideRight
        }

        DownloadMark {
            id: cardMark

            x: Math.min(nameText.contentWidth, nameText.width) + 6
            y: nameText.y + (nameText.height - height) / 2
            remoteUrl: card.remoteUrl
        }

        AppText {
            id: metaText

            y: nameText.y + nameText.height
            width: card.width
            visible: card.subtitle !== ""
            horizontalAlignment: card.centred ? Text.AlignHCenter : Text.AlignLeft
            text: card.subtitle
            font: Theme.cardSubtitle
            lineHeight: Theme.lineHeightCardSubtitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            elide: Text.ElideRight
        }

        AppText {
            y: metaText.y + metaText.height + 2
            width: card.width
            visible: card.dateText !== ""
            horizontalAlignment: card.centred ? Text.AlignHCenter : Text.AlignLeft
            text: card.dateText
            font: Theme.cardDate
            color: Theme.fgTertiary
            elide: Text.ElideRight
        }
    }

    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: card.contentItem
    }
}
