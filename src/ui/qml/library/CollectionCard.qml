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
    required artworkLabel
    required round
    required property color gradientStart
    required property color gradientEnd
    required property string iconName

    // Only the Library's own artist cards centre their text.
    centred: kind === "artist"
    // Every playlist, mix, album and genre card has the collection menu;
    // artists have their own.
    hasMenu: kind !== "artist" && kind !== "catalog"
    art: kind === "system" || kind === "mix" ? systemArt : (kind === "genre" ? genreArt : null)

    Component {
        id: systemArt

        Rectangle {
            radius: Theme.radiusXl
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: card.gradientStart }
                GradientStop { position: 1; color: card.gradientEnd }
            }

            Icon {
                anchors.centerIn: parent
                name: card.iconName
                size: Math.round(parent.width * 0.33)
                color: "white"
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
