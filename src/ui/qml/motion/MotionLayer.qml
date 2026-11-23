pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The small celebrations over the whole window (App.effects), each made for
// its moment and gone after it; nothing here takes input:
// - the queue arc (queue-fly.ts, motion.css 329-340): a 44px cover rises,
//   arcs over and drops onto the queue in 640 ms, which bumps as it lands;
// - the heart burst (heart-bloom.ts, motion.css 54-91): the heart pops, a
//   ring spreads and seven dots scatter from it;
// - the save motion (save-motion.ts): the save button pops and a ring
//   spreads from it.
// All are skipped with reduced motion.
Item {
    id: layer

    // Pops an item: scale through the keyframes, on the water curve.
    function pop(item, peak, dip) {
        if (!item)
            return
        popComponent.createObject(layer, { target: item, peak: peak, dip: dip })
    }

    function queueTarget() {
        for (const entry of App.effects.queueTargets()) {
            const item = entry.item
            if (!item || !item.visible || item.width <= 0)
                continue
            const point = item.mapToItem(layer, item.width * entry.fractionX, item.height / 2)
            if (point.x < 0 || point.y < 0 || point.x > layer.width || point.y > layer.height)
                continue
            return { item: item, point: point }
        }
        return null
    }

    Connections {
        target: App.effects

        function onQueueArc(from, source, artworkId, label) {
            if (Motion.reduced)
                return
            const target = layer.queueTarget()
            if (!target)
                return
            const start = layer.mapFromItem(null, from.x, from.y)
            arcComponent.createObject(layer, {
                startX: start.x, startY: start.y, endX: target.point.x, endY: target.point.y,
                source: source, artworkId: artworkId, label: label, landing: target.item
            })
        }

        function onHeartBurst(heart) {
            if (Motion.reduced || !heart)
                return
            layer.pop(heart, 1.32, 0.9)
            const size = Math.max(heart.width, heart.height) * 2.6
            const centre = heart.mapToItem(layer, heart.width / 2, heart.height / 2)
            burstComponent.createObject(layer, { x: centre.x - size / 2, y: centre.y - size / 2, size: size })
        }

        function onSaveBurst(button) {
            if (Motion.reduced || !button)
                return
            layer.pop(button, 1.28, 0.92)
            const box = button.mapToItem(layer, -2, -2, button.width + 4, button.height + 4)
            ringComponent.createObject(layer, { x: box.x, y: box.y, width: box.width, height: box.height })
        }
    }

    Component {
        id: popComponent

        SequentialAnimation {
            id: popping

            property Item target
            property real peak: 1.3
            property real dip: 0.9

            running: true
            onFinished: popping.destroy()

            NumberAnimation { target: popping.target; property: "scale"; from: 1; to: popping.peak; duration: 161; easing: Motion.water }
            NumberAnimation { target: popping.target; property: "scale"; to: popping.dip; duration: 138; easing: Motion.water }
            NumberAnimation { target: popping.target; property: "scale"; to: 1; duration: 161; easing: Motion.water }
        }
    }

    // The queue arc: keyframes at 0, 12%, 55% and 100% of the eased time.
    Component {
        id: arcComponent

        Item {
            id: chip

            property real startX
            property real startY
            property real endX
            property real endY
            property url source
            property string artworkId
            property string label
            property Item landing
            // Driven by the animation below, from 0 to 1.
            property real progress

            readonly property real dx: endX - startX
            readonly property real dy: endY - startY
            readonly property real lift: Math.min(160, Math.max(60, Math.abs(dx) * 0.25))

            // Linear between the keyframes, like the reference's animation.
            function segment(a, b, from, to) {
                return from + (to - from) * Math.max(0, Math.min(1, (progress - a) / (b - a)))
            }

            readonly property real tx: progress < 0.12 ? 0 : progress < 0.55 ? segment(0.12, 0.55, 0, dx * 0.5)
                                                                              : segment(0.55, 1, dx * 0.5, dx)
            readonly property real ty: progress < 0.12 ? segment(0, 0.12, 0, -8)
                                       : progress < 0.55 ? segment(0.12, 0.55, -8, dy * 0.5 - lift)
                                                         : segment(0.55, 1, dy * 0.5 - lift, dy)

            x: startX - 22 + tx
            y: startY - 22 + ty
            width: 44
            height: 44
            scale: progress < 0.12 ? segment(0, 0.12, 0.6, 1) : progress < 0.55 ? segment(0.12, 0.55, 1, 0.85)
                                                                                : segment(0.55, 1, 0.85, 0.3)
            opacity: progress < 0.12 ? segment(0, 0.12, 0, 1) : progress < 0.55 ? 1 : segment(0.55, 1, 1, 0.4)

            ArtTile {
                anchors.fill: parent
                radius: 10
                shadow: true
                source: chip.source
                artworkId: chip.artworkId
                label: chip.label
            }

            NumberAnimation on progress {
                from: 0
                to: 1
                duration: 640
                easing: Motion.queueArc
                onFinished: {
                    if (chip.landing)
                        bump.start()
                    else
                        chip.destroy()
                }
            }

            // The queue gives a little bump as the cover lands.
            SequentialAnimation {
                id: bump

                onStarted: chip.visible = false
                onFinished: chip.destroy()

                NumberAnimation { target: chip.landing; property: "scale"; from: 1; to: 1.12; duration: 112; easing.type: Easing.OutQuad }
                NumberAnimation { target: chip.landing; property: "scale"; to: 1; duration: 168; easing.type: Easing.OutQuad }
            }
        }
    }

    // The heart burst: a ring at 22% inset and seven dots.
    Component {
        id: burstComponent

        Item {
            id: burst

            property real size
            readonly property real reach: size * 0.45

            width: size
            height: size

            Timer {
                interval: 720
                running: true
                onTriggered: burst.destroy()
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: burst.size * 0.22
                radius: width / 2
                color: "transparent"
                border.width: 2
                border.color: Theme.accentColor

                NumberAnimation on scale {
                    from: 0.3
                    to: 1
                    duration: 520
                    easing: Motion.water
                }
                NumberAnimation on opacity {
                    from: 0.9
                    to: 0
                    duration: 520
                    easing: Motion.water
                }
            }

            Repeater {
                model: 7

                Item {
                    id: dot

                    required property int index
                    readonly property real angle: (360 / 7 * index + 12) * Math.PI / 180
                    readonly property real side: index % 2 === 1 ? 4 : 5
                    property real travel: 0.3

                    x: burst.size / 2 + Math.sin(angle) * burst.reach * travel - side / 2
                    y: burst.size / 2 - Math.cos(angle) * burst.reach * travel - side / 2
                    width: side
                    height: side
                    opacity: 0

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: Theme.accentColor
                    }

                    SequentialAnimation {
                        running: true

                        PauseAnimation { duration: 60 }
                        PropertyAction { target: dot; property: "opacity"; value: 1 }
                        ParallelAnimation {
                            NumberAnimation { target: dot; property: "travel"; from: 0.3; to: 1; duration: 640; easing: Motion.water }
                            NumberAnimation { target: dot; property: "scale"; from: 1; to: 0.4; duration: 640; easing: Motion.water }
                            SequentialAnimation {
                                PauseAnimation { duration: 448 }
                                NumberAnimation { target: dot; property: "opacity"; to: 0; duration: 192 }
                            }
                        }
                    }
                }
            }
        }
    }

    // The save ring: spreads from the button and fades, 560 ms.
    Component {
        id: ringComponent

        Rectangle {
            id: ring

            color: "transparent"
            radius: height / 2
            border.width: 2
            border.color: Theme.accentColor

            ParallelAnimation {
                running: true
                onFinished: ring.destroy()

                NumberAnimation { target: ring; property: "scale"; from: 0.6; to: 1.9; duration: 560; easing.type: Easing.OutQuad }
                NumberAnimation { target: ring; property: "opacity"; from: 0.9; to: 0; duration: 560; easing.type: Easing.OutQuad }
            }
        }
    }
}
