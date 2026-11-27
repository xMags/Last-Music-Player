import QtQuick
import LastMusicPlayer

// The light band that sweeps across a group of loading placeholders: one
// band per group, tilted like a 105 degree gradient, transparent to 35% of
// its width, `bandColor` at 75% in the middle, transparent from 65%. Per
// Motion.skeletonSweep pass it crosses from fully off the left edge to fully
// off the right during the first 55%, easing in and out, then rests.
//
// The band is the colour of what the group sits on, so it shows only over
// the tinted blocks: the content panel's (the middle of its gradient) by
// default, where the pages' skeletons sit; a group elsewhere sets it. One
// gradient rectangle stepped by the shared 60-a-second clock (AmbientPhase),
// not a display-rate animation. Fill the group with it, above the blocks.
// Nothing moves and no band is drawn under reduced motion, while the window
// is hidden or while the group is not visible.
Item {
    id: root

    property color bandColor: Theme.mixSrgb(Theme.panelGrad1, Theme.panelGrad2, 0.5)

    // The band's travel through the pass: 0 off the left, 1 off the right.
    readonly property real travel: {
        const t = Math.min(1, clock.phase / 0.55)
        return 0.5 - 0.5 * Math.cos(Math.PI * t)
    }
    // How far the tilt reaches past the band's own width on each side.
    readonly property real overhang: root.height * 0.27

    clip: true

    AmbientPhase {
        id: clock

        running: !Motion.reduced && Motion.windowShown && root.visible && root.width > 0
        held: Motion.windowMoving
        period: Motion.skeletonSweep
    }

    Rectangle {
        // As wide as the group, so the visible band is 30% of it; tall enough
        // to cover the group once tilted 15 degrees.
        width: root.width
        height: root.height * 1.2 + root.width * 0.3
        y: (root.height - height) / 2
        x: -0.65 * root.width - root.overhang
           + root.travel * (root.width + 0.3 * root.width + 2 * root.overhang)
        rotation: 15
        visible: clock.active
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.35; color: Qt.alpha(root.bandColor, 0) }
            GradientStop { position: 0.5; color: Qt.alpha(root.bandColor, 0.75) }
            GradientStop { position: 0.65; color: Qt.alpha(root.bandColor, 0) }
        }
    }
}
