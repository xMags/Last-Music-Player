import QtQuick
import LastMusicPlayer

// One song in the grid form (common.ts gridCard): a Card with the cover, the
// title and the artist. The playing song's title takes the accent.
Card {
    id: tile

    required title
    required artworkUrl
    required artworkId
    required artworkLabel
    required property string artist
    required property bool playing
    required property bool unavailable
    required property string unavailableReason
    required remoteUrl

    subtitle: unavailable ? unavailableReason : artist
    opacity: unavailable ? 0.55 : 1
}
