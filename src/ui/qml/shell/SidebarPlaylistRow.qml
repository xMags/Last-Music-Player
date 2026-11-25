pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// A playlist in the sidebar (.playlist-item, styles.css 346-367): a 32px
// swatch beside the name, 14px apart. Liked Songs has the accent gradient
// swatch with a heart; other playlists their cover mosaic (playlist-art.ts)
// over the art gradient with a 1px inset stroke, or a plain swatch with a
// note when no song has a cover. The glyphs are 20px (.nav-item .icon).
NavItem {
    id: row

    property bool liked: false
    // The "All playlists" row: a "..." in the swatch.
    property bool more: false
    // The cover (PlaylistCoverArt): "collage" or "image".
    property string coverStyle: "image"
    property url artworkUrl
    property var collage: []

    readonly property bool hasArt: !liked && !more && (coverStyle === "collage" || artworkUrl.toString() !== "")

    gap: 14
    leading: Item {
        implicitWidth: 32
        implicitHeight: 32

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: Theme.neutral
            visible: !row.liked && !row.hasArt
        }

        // linear-gradient(135deg, accent-fill-2, accent-fill) for Liked
        // Songs, or the art gradient behind a cover.
        DiagonalGradient {
            anchors.fill: parent
            visible: row.liked || row.hasArt
            radius: Theme.radiusSm
            startColor: row.liked ? Theme.accentFill2 : Theme.artGrad1
            endColor: row.liked ? Theme.accentFill : Theme.artGrad2
        }

        // Only a row on show fetches its covers: rows left out for height
        // would hold back the pictures that are on screen.
        Loader {
            anchors.fill: parent
            active: row.hasArt && row.visible
            sourceComponent: CollectionCover {
                coverStyle: row.coverStyle
                source: row.artworkUrl
                collage: row.collage
                radius: Theme.radiusSm
                plain: true
            }
        }

        // box-shadow: inset 0 0 0 1px var(--stroke)
        Rectangle {
            anchors.fill: parent
            visible: row.hasArt
            radius: Theme.radiusSm
            color: "transparent"
            border.width: 1
            border.color: Theme.stroke
        }

        Icon {
            anchors.centerIn: parent
            visible: !row.hasArt
            name: row.liked ? "heart-fill" : row.more ? "more" : "music-note"
            size: 20
            color: row.liked ? Theme.onOverlay : Theme.fgTertiary
        }
    }
}
