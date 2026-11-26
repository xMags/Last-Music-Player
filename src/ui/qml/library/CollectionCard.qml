pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A card of a CollectionCardModel: a system playlist shows its gradient and
// icon, a genre its drawn tile, an artist a round picture, everything else
// its cover or lettered placeholder.
Card {
    id: card

    required property string kind
    required title
    required subtitle
    required artworkUrl
    required artworkId
    required round
    required property color gradientStart
    required property color gradientEnd
    required property string iconName
    // A mix's badge, drawn on its cover.
    property string badge: ""

    // Only the Library's own artist cards centre their text.
    centred: kind === "artist"
    // Every playlist, mix, album and genre card has the collection menu;
    // artists have their own.
    hasMenu: kind !== "artist" && kind !== "catalog"
    art: kind === "system" ? systemArt : kind === "mix" ? mixArt : (kind === "genre" ? genreArt : null)

    Component {
        id: systemArt

        // linear-gradient(135deg) (views.css 274-289); the icon at the size
        // playlist-art.ts draws it: 52px for the heart, 56px otherwise.
        DiagonalGradient {
            radius: Theme.radiusXl
            startColor: card.gradientStart
            endColor: card.gradientEnd

            Icon {
                anchors.centerIn: parent
                name: card.iconName
                size: card.iconName === "heart-fill" ? 52 : 56
                color: "white"
            }
        }
    }

    // library.ts mixArt: the 135deg gradient, the badge at the top left
    // (10px 800, 0.08em, white at 92%) and a 44px sparkle at the bottom
    // right at 45% opacity, both 10px and 12px in.
    Component {
        id: mixArt

        DiagonalGradient {
            radius: Theme.radiusXl
            startColor: card.gradientStart
            endColor: card.gradientEnd

            Text {
                x: 12
                y: 10
                text: card.badge
                font: Theme.mixCoverBadge
                color: Qt.rgba(1, 1, 1, 0.92)
            }

            Icon {
                x: parent.width - width - 10
                y: parent.height - height - 10
                name: "sparkle"
                size: 44
                color: "white"
                opacity: 0.45
            }
        }
    }

    Component {
        id: genreArt

        GenreTile {
            genre: card.title
            radius: Theme.radiusXl
        }
    }
}
