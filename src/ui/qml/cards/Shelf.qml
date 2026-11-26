pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A horizontal row of cards with its heading (.row-scroll and .row-nav,
// styles.css 555-562 and 1046-1099): 156px cards 20px apart, no scroll bar,
// and round arrow buttons at the covers' middle that appear while the row is
// hovered and only where there is more to scroll to. The wheel and the
// arrows scroll it; a page of cards per arrow. The row runs edge to edge:
// it reaches through the page's side gutter (`bleed`), so cards clip at the
// panel's edge rather than in the middle of the gutter, and a matching
// margin keeps the first card under the heading at rest.
Column {
    id: shelf

    property string title: ""
    property string pill: ""
    property string linkText: ""
    property var model
    property alias delegate: row.delegate
    // False keeps the cards unbuilt and shows skeleton cards in their place,
    // at the same height; a page sets it while the shelf waits its turn.
    property bool cardsBuilt: true
    // The skeleton cards are round (artist shelves).
    property bool skeletonRound: false
    property int cardWidth: 156
    property int cardSpacing: 20
    // A card's height: the cover, 12px, then two lines of text.
    property int cardHeight: cardWidth + 12 + 22 + 18
    // How far the row reaches past each side: the page's gutter.
    property real bleed: Theme.contentGutter

    signal linkClicked()

    spacing: 16

    // A shelf under a heading of its own (search results) has no header.
    SectionHeader {
        pill: shelf.pill
        width: shelf.width
        visible: shelf.title !== "" || shelf.linkText !== ""
        title: shelf.title
        linkText: shelf.linkText
        onLinkClicked: shelf.linkClicked()
    }

    Item {
        id: viewport

        width: shelf.width
        height: shelf.cardHeight + 8

        HoverHandler {
            id: hover
        }

        Loader {
            width: parent.width
            height: parent.height
            active: !shelf.cardsBuilt
            sourceComponent: ShelfSkeleton {
                cardWidth: shelf.cardWidth
                cardHeight: shelf.cardHeight
                cardSpacing: shelf.cardSpacing
                round: shelf.skeletonRound
            }
        }

        ListView {
            id: row

            model: shelf.cardsBuilt ? shelf.model : null
            x: -shelf.bleed
            width: parent.width + 2 * shelf.bleed
            height: parent.height
            leftMargin: shelf.bleed
            rightMargin: shelf.bleed
            orientation: ListView.Horizontal
            spacing: shelf.cardSpacing
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            reuseItems: true

            Behavior on contentX {
                id: glide

                enabled: false
                NumberAnimation { duration: Motion.page; easing: Motion.water }
            }

            function scrollPage(direction) {
                // scrollBy(clientWidth * 0.85), as carousel.ts steps.
                const page = width * 0.85
                const first = originX - leftMargin
                const last = originX + contentWidth + rightMargin - width
                const target = Math.max(first, Math.min(contentX + direction * page, last))
                glide.enabled = !Motion.reduced
                contentX = target
                glide.enabled = false
            }
        }

        ShelfArrow {
            Accessible.name: qsTr("Scroll left")
            x: -10
            y: shelf.cardWidth / 2 - height / 2
            iconName: "chevron"
            pointsBack: true
            shown: hover.hovered && !row.atXBeginning
            onClicked: row.scrollPage(-1)
        }

        ShelfArrow {
            Accessible.name: qsTr("Scroll right")
            x: viewport.width - width + 10
            y: shelf.cardWidth / 2 - height / 2
            iconName: "chevron"
            shown: hover.hovered && !row.atXEnd
            onClicked: row.scrollPage(1)
        }
    }
}
