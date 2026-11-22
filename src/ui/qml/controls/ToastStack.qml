pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The column of toasts (toast.ts; styles.css 1862-1895), newest at the bottom.
// A new toast springs up from below, a leaving one shrinks away, and the rest
// glide to their new places. The owner sizes the stack to the area the toasts
// may use; they stack upwards from its bottom edge (104px above the window's
// bottom in the shell). The queue holds the newest toast first, so the list
// is laid out from the bottom up.
Item {
    id: root

    required property ToastQueue queue

    implicitWidth: 420

    ListView {
        id: list

        anchors.fill: parent
        interactive: false
        verticalLayoutDirection: ListView.BottomToTop
        spacing: 8
        model: root.queue

        remove: Transition {
            enabled: !Motion.reduced
            NumberAnimation { property: "opacity"; to: 0; duration: Motion.toastExit; easing: Motion.easeIn }
            NumberAnimation { property: "exitProgress"; to: 1; duration: Motion.toastExit; easing: Motion.easeIn }
        }
        displaced: Transition {
            enabled: !Motion.reduced
            NumberAnimation { property: "y"; duration: Motion.toastReflow; easing: Motion.water }
        }

        delegate: Item {
            id: slot

            required property int toastId
            required property string text
            required property bool error
            required property string actionLabel

            // 0 to 1 (and past it, with the spring) as the toast arrives; the
            // list's remove transition runs exitProgress as it leaves.
            property real enterProgress: 1
            property real exitProgress: 0

            ListView.onAdd: {
                if (!Motion.reduced)
                    arrival.start()
            }

            NumberAnimation {
                id: arrival

                target: slot
                property: "enterProgress"
                from: 0
                to: 1
                duration: Motion.toastEnter
                easing: Motion.toastSpring
            }

            width: list.width
            height: toast.height

            Toast {
                id: toast

                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, slot.width)
                text: slot.text
                error: slot.error
                actionLabel: slot.actionLabel
                opacity: Motion.toastEnterOpacity(slot.enterProgress)
                // Leaving: down 6px and scaled to 0.94.
                scale: Motion.toastEnterScale(slot.enterProgress) - 0.06 * slot.exitProgress
                transform: Translate { y: Motion.toastEnterOffset(slot.enterProgress) + 6 * slot.exitProgress }
                onActionPressed: root.queue.triggerAction(slot.toastId)
            }
        }
    }
}
