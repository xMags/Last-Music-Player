pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The playing indicator of a track row (.eq-anim, views.css 213-228): three
// 2px accent bars, 8, 14 and 10px tall, each growing and shrinking between
// 40% and full height over 0.9s, a fifth of a second apart. The reference's
// fallback without audio levels (D12). While playback is paused (`running`
// false) they sink to 30% in 320 ms instead of bouncing on (motion.css
// 105-109); with reduced motion they stand still at 60%.
Row {
    id: bars

    property bool running: true

    spacing: 2
    height: 14

    AmbientPhase {
        id: beat

        running: bars.running && bars.visible && !Motion.reduced && Motion.windowShown
        held: Motion.windowMoving
        period: 900
    }

    Repeater {
        model: [8, 14, 10]

        Rectangle {
            id: bar

            required property int index
            required property int modelData

            // Standing on the bottom edge; a position, not an anchor, since a
            // row put aside for reuse leaves the bar briefly without a parent.
            y: bars.height - height
            width: 2
            height: modelData
            radius: 1
            color: Theme.accentColor
            transformOrigin: Item.Bottom
            readonly property real rest: Motion.reduced ? 0.6 : 0.3
            readonly property bool moving: bars.running && bars.visible && !Motion.reduced && Motion.windowShown
            // What the bar shows while it does not bounce: at rest, or on
            // its way down to it.
            property real held: rest

            scale: bounce.holding ? held : bounce.value

            // Stopped: sink from where the bounce left the bar down to rest.
            // While moving again, the bar keeps its place until its delay
            // has passed.
            onMovingChanged: {
                if (bar.moving) {
                    sink.stop()
                    return
                }
                if (Motion.reduced) {
                    sink.stop()
                    bar.held = bar.rest
                } else {
                    // The clock holds the stopped run's time: past the delay,
                    // the bar showed the bounce; before it, the held value.
                    sink.from = beat.elapsed >= bounce.delay ? bounce.value : bar.held
                    sink.restart()
                }
            }

            NumberAnimation {
                id: sink

                target: bar
                property: "held"
                to: bar.rest
                duration: 320
                easing: Motion.water
            }

            // The bounce: after the bar's delay, 0.4 to full height and back,
            // 450 ms each way. The bars share one clock, so they keep their
            // fifth of a second apart.
            AmbientBounce {
                id: bounce

                clock: beat
                delay: bar.index * 200
                halfPeriod: 450
                from: 0.4
                to: 1
                easing.type: Easing.InOutQuad
            }
        }
    }
}
