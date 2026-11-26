pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// One loading placeholder block: the accent tinted into the surface, no
// border. Title lines use the stronger tint. A cover block can carry the
// line glyph (`glyph`: "note" or "person"). Groups of blocks share one sweep
// (SkeletonRows, SkeletonGrid, ShelfSkeleton); a block used on its own sets
// `sweep`.
Rectangle {
    id: block

    property bool sweep: false
    property bool strong: false
    // "" for none, "note" or "person".
    property string glyph: ""

    radius: Theme.radiusMd
    color: block.strong ? Theme.skeletonStrong : Theme.skeleton

    // Built in the frames after the block appears (see ArtTile).
    Loader {
        anchors.centerIn: parent
        asynchronous: true
        active: block.glyph !== "" && Math.min(block.width, block.height) >= 32
        sourceComponent: SkeletonGlyph {
            side: Math.min(block.width, block.height)
            person: block.glyph === "person"
        }
    }

    // Made only for a block that sweeps on its own: most share a group's.
    Loader {
        anchors.fill: parent
        active: block.sweep
        sourceComponent: SkeletonSweep {}
    }
}
