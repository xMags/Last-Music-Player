pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The Lyrics / Up Next switch of the full-screen player (.fs-tabs, .fs-tab,
// styles.css 2218-2241): two text tabs on a faint glass strip, 3px inside a
// 1px border, 2px apart. Picking the chosen tab again hides the side panel.
// `stretch`: the tabs share the width (heading the side panel); otherwise
// each is as wide as its label (in the top bar).
Rectangle {
    id: root

    property bool stretch: false

    implicitWidth: row.implicitWidth + 2 * 4
    implicitHeight: row.implicitHeight + 2 * 4
    radius: 10
    color: Theme.fullscreenTabsFill
    border.width: 1
    border.color: Theme.fullscreenTabsBorder

    // .fs-tabs: backdrop-filter: blur(12px) (styles.css 2217-2224).
    BackdropBlur {
        z: -1
        source: App.effects.fullscreenBackdrop
        blurPx: 12
        cornerRadius: root.radius
    }

    component Tab: T.AbstractButton {
        id: tab

        required property string label
        required property bool chosen

        Layout.fillWidth: root.stretch
        implicitWidth: caption.implicitWidth + 2 * 18
        implicitHeight: caption.implicitHeight + 2 * 7
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: AppText {
            id: caption

            text: tab.label
            font: tab.chosen ? Theme.fullscreenTabActive : Theme.fullscreenTab
            color: tab.chosen || tab.hovered ? Theme.fullscreenText : Theme.fullscreenTabText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter

            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
            }
        }

        background: Rectangle {
            radius: 7
            color: tab.chosen ? Theme.fullscreenTabActiveFill : "transparent"

            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
            }

            FocusRing {
                shown: tab.visualFocus
                controlRadius: 7
            }
        }
    }

    RowLayout {
        id: row

        anchors.fill: parent
        anchors.margins: 4
        spacing: 2

        Tab {
            label: qsTr("Lyrics")
            chosen: App.fullscreen.lyricsChosen
            onClicked: App.fullscreen.pickLyrics()
        }

        Tab {
            label: qsTr("Up Next")
            chosen: App.fullscreen.queueChosen
            onClicked: App.fullscreen.pickQueue()
        }
    }
}
