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
            scale: Motion.reduced ? 0.6 : 0.3

            // Paused: sink to rest.
            Behavior on scale {
                enabled: !Motion.reduced && !bars.running
                NumberAnimation { duration: 320; easing: Motion.water }
            }

            SequentialAnimation on scale {
                running: bars.running && bars.visible && !Motion.reduced
                loops: Animation.Infinite
                alwaysRunToEnd: false

                PauseAnimation { duration: bar.index * 200 }
                SequentialAnimation {
                    loops: Animation.Infinite

                    NumberAnimation { from: 0.4; to: 1; duration: 450; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 1; to: 0.4; duration: 450; easing.type: Easing.InOutQuad }
                }
            }
        }
    }
}
