pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A segmented switch. The selected choice sits on a raised pill that slides to
// a new choice (segment-glide.ts: 260 ms) when the user picks one.
//   Default - the tab strip (.tab-strip, styles.css 1193-1219): choices share
//             the width equally, 14px labels, the selected one semibold.
//   Compact - the inline switch (.segmented, views.css 102-149): each choice
//             as wide as its label, 12px semibold labels.
// `choices` is a short fixed list of labels.
Rectangle {
    id: root

    required property var choices
    property int currentIndex: 0
    property bool compact: false
    // Set on the first pick, so the pill does not slide in at start-up.
    property bool glide: false

    signal activated(int index)

    implicitWidth: compact ? strip.contentWidth + 2 * 4 : 240
    implicitHeight: (compact ? 26 : 34) + 2 * 4
    radius: Theme.radiusMd
    color: Theme.segmentTrack
    border.width: 1
    border.color: Theme.stroke

    ListView {
        id: strip

        // 1px border plus 3px padding.
        anchors.fill: parent
        anchors.margins: 4
        orientation: ListView.Horizontal
        interactive: false
        spacing: 2
        model: root.choices
        currentIndex: root.currentIndex
        highlightFollowsCurrentItem: false
        keyNavigationEnabled: false

        highlight: Item {
            x: strip.currentItem ? strip.currentItem.x : 0
            width: strip.currentItem ? strip.currentItem.width : 0
            height: strip.height

            Behavior on x {
                enabled: root.glide && !Motion.reduced
                NumberAnimation { duration: Motion.segmentGlide; easing: Motion.segment }
            }
            Behavior on width {
                enabled: root.glide && !Motion.reduced
                NumberAnimation { duration: Motion.segmentGlide; easing: Motion.segment }
            }

            BoxShadow {
                anchors.fill: parent
                layers: Theme.shadow
                radius: Theme.radiusSm
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Theme.segmentSelected
            }
        }

        delegate: T.AbstractButton {
            id: segment

            required property string modelData
            required property int index
            readonly property bool selected: index === root.currentIndex

            width: root.compact ? Math.max(56, implicitWidth) : (strip.width - strip.spacing * (strip.count - 1)) / strip.count
            height: strip.height
            implicitWidth: label.implicitWidth + 2 * 14
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            text: modelData

            onClicked: {
                root.glide = true
                root.activated(index)
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            contentItem: Text {
                id: label

                text: segment.text
                font: root.compact ? Theme.segmentCompactLabel : segment.selected ? Theme.segmentSelectedLabel : Theme.segmentLabel
                color: segment.selected || segment.hovered ? Theme.fg : Theme.fgSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight

                Behavior on color {
                    enabled: !Motion.reduced
                    ColorAnimation { duration: Motion.chipFade; easing: Motion.standard }
                }
            }

            background: Item {
                FocusRing {
                    shown: segment.visualFocus
                    controlRadius: Theme.radiusSm
                    offset: -2
                }
            }
        }
    }
}
