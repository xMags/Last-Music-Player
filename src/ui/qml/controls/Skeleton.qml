import QtQuick
import LastMusicPlayer

// One loading placeholder block (.lm-skeleton-block, styles.css 680-686): a
// soft filled shape where content will appear. Groups of blocks share one
// sweep (SkeletonRows, SkeletonGrid); a block used on its own sets `sweep`.
Rectangle {
    id: block

    property bool sweep: false
    // Titles use the stronger stroke colour as their fill.
    property bool strong: false

    radius: Theme.radiusMd
    color: strong ? Theme.strokeStrong : Theme.hover
    border.width: 1
    border.color: Theme.stroke

    SkeletonSweep {
        anchors.fill: parent
        visible: block.sweep
    }
}
