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
    property url artworkUrl
    property string artworkId: ""
    property string artworkLabel: ""
    // An artist: the cover is a circle and the text is centred.
    property bool round: false
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

    Keys.onMenuPressed: card.menuRequested(Qt.point(width / 2, width / 2), true)

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => card.menuRequested(eventPoint.position, false)
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
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

            // The picture zooms inside a clipped frame.
            Item {
                anchors.fill: parent
                clip: true

                Item {
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
                        label: card.artworkLabel
                    }

                    Loader {
                        anchors.fill: parent
                        active: card.art !== null
                        sourceComponent: card.art
                    }
                }
            }

            IconButton {
                id: more

                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                visible: card.hasMenu && !card.round
                opacity: card.hovered || card.visualFocus || more.visualFocus ? 1 : 0
                iconName: "more"
                shape: IconButton.Overlay
                Accessible.name: qsTr("More options")
                onClicked: card.menuRequested(more.mapToItem(card, more.width / 2, more.height), false)

                Behavior on opacity {
                    enabled: !Motion.reduced
                    NumberAnimation { duration: Motion.chipFade }
                }
            }

            FocusRing {
                shown: card.visualFocus
                controlRadius: card.round ? width / 2 : Theme.radiusXl
            }
        }

        Text {
            id: nameText

            y: cover.height + 12
            width: card.width
            horizontalAlignment: card.round ? Text.AlignHCenter : Text.AlignLeft
            text: card.title
            font: Theme.cardTitle
            lineHeight: Theme.lineHeightCardTitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
            elide: Text.ElideRight
        }

        Text {
            id: metaText

            y: nameText.y + nameText.height
            width: card.width
            visible: card.subtitle !== ""
            horizontalAlignment: card.round ? Text.AlignHCenter : Text.AlignLeft
            text: card.subtitle
            font: Theme.cardSubtitle
            lineHeight: Theme.lineHeightCardSubtitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            elide: Text.ElideRight
        }
    }
}
