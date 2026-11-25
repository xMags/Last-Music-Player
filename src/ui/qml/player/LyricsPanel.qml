pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The lyrics of the playing song (App.lyrics; views/queue.ts 834-975 and
// the .lyrics-* rules of styles.css 1994-2015, 2288-2310). Synced lines
// follow playback: the playing line lights up and scrolls to the middle,
// unless the reader has scrolled away, until they click a line, which also
// seeks there. Plain lyrics read as text; a song without lyrics, an
// instrumental and a failed request show a centred notice, the failure with
// "Try again". `fullscreen` gives the full-screen player's larger lines on
// a dark panel.
Item {
    id: panel

    property bool fullscreen: false

    // Whether the playing line scrolls into view; reading elsewhere stops it.
    property bool follow: true

    readonly property string state: App.lyrics.state
    readonly property color lineColor: fullscreen ? Qt.rgba(1, 1, 1, 0.36) : Theme.fgTertiary
    readonly property color activeColor: fullscreen ? "#ffffff" : Theme.fg
    readonly property color plainColor: fullscreen ? Qt.rgba(1, 1, 1, 0.82) : Theme.fgSecondary
    readonly property color headingColor: fullscreen ? Theme.fullscreenText : Theme.fg
    readonly property color detailColor: fullscreen ? Theme.fullscreenSecondaryText : Theme.fgSecondary

    function centreActive(): void {
        if (!panel.follow || App.lyrics.activeLine < 0 || lines.count === 0) {
            return
        }
        const item = lines.itemAtIndex(App.lyrics.activeLine)
        if (item === null) {
            lines.positionViewAtIndex(App.lyrics.activeLine, ListView.Center)
            return
        }
        const target = item.y + item.height / 2 - lines.height / 2
        scroll.to = Math.max(lines.originY - lines.topMargin,
                             Math.min(target, lines.contentHeight + lines.originY - lines.height + lines.bottomMargin))
        scroll.restart()
    }

    Connections {
        target: App.lyrics

        function onActiveLineChanged(): void {
            panel.centreActive()
        }

        function onChanged(): void {
            if (App.lyrics.state !== "synced") {
                panel.follow = true
            }
        }
    }

    // Loading: a few soft bars (.lm-skeleton-lyrics).
    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 8
        spacing: 15
        visible: panel.state === "loading"

        Repeater {
            model: [1.0, 0.82, 0.94, 0.7, 0.88, 0.76]

            Skeleton {
                id: bar

                required property real modelData

                width: panel.width * bar.modelData
                height: panel.fullscreen ? 16 : 13
                radius: height / 2
                sweep: true
            }
        }
    }

    // Synced lines.
    ListView {
        id: lines

        anchors.fill: parent
        visible: panel.state === "synced"
        clip: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Lyrics")
        model: panel.state === "synced" ? App.lyrics.lines : []
        boundsBehavior: Flickable.StopAtBounds
        // Room above the first line and below the last, so either can sit
        // in the middle.
        topMargin: panel.fullscreen ? height / 2 - 30 : 4
        bottomMargin: panel.fullscreen ? height / 2 - 30 : 24

        onMovementStarted: panel.follow = false
        onModelChanged: {
            panel.follow = true
            positionViewAtBeginning()
        }

        // A drag or the wheel: the user reads on their own.
        WheelScroll {
            flickable: lines
            onScrolled: panel.follow = false
        }

        NumberAnimation {
            id: scroll

            target: lines
            property: "contentY"
            duration: 450
            easing.type: Easing.OutCubic
        }

        delegate: Rectangle {
            id: line

            required property int index
            required property string modelData

            readonly property bool active: index === App.lyrics.activeLine
            // .lyrics-line.active + .lyrics-line: the next line comes up a little.
            readonly property bool next: App.lyrics.activeLine >= 0 && index === App.lyrics.activeLine + 1

            width: ListView.view.width
            height: label.implicitHeight + (panel.fullscreen ? 16 : 10)
            radius: panel.fullscreen ? 10 : 6
            color: hover.hovered ? (panel.fullscreen ? Theme.fullscreenRowHover : Theme.hover) : "transparent"

            Text {
                id: label

                x: panel.fullscreen ? 10 : 8
                width: parent.width - 2 * x
                anchors.verticalCenter: parent.verticalCenter
                text: line.modelData.length > 0 ? line.modelData : "…"
                wrapMode: Text.Wrap
                font.family: Theme.body.family
                font.pixelSize: panel.fullscreen ? 26 : 14
                font.weight: panel.fullscreen || line.active ? (panel.fullscreen ? Font.Bold : Font.DemiBold) : Font.Normal
                lineHeight: panel.fullscreen ? 1.35 : 1.7
                color: line.active ? panel.activeColor
                    : line.next ? (panel.fullscreen ? Qt.rgba(1, 1, 1, 0.58) : Theme.mixSrgb(Theme.fg, "transparent", 0.62))
                                : panel.lineColor
                // motion.css 295-304: the sung line grows 4.5% from its start.
                transformOrigin: Item.Left
                scale: line.active && !Motion.reduced ? 1.045 : 1

                Behavior on color {
                    enabled: !Motion.reduced
                    ColorAnimation { duration: 350; easing.type: Easing.InOutQuad }
                }

                Behavior on scale {
                    enabled: !Motion.reduced
                    NumberAnimation { duration: 450; easing.type: Easing.Bezier; easing.bezierCurve: [0.22, 0.61, 0.36, 1, 1, 1] }
                }
            }

            HoverHandler {
                id: hover

                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: {
                    panel.follow = true
                    App.lyrics.seekToLine(line.index)
                }
            }

            Accessible.role: Accessible.Button
            Accessible.name: label.text
            Accessible.onPressAction: {
                panel.follow = true
                App.lyrics.seekToLine(line.index)
            }
        }
    }

    // Plain lyrics.
    Flickable {
        id: plainScroll

        anchors.fill: parent
        visible: panel.state === "plain"
        clip: true
        contentHeight: plainText.implicitHeight + 28
        boundsBehavior: Flickable.StopAtBounds

        WheelScroll { flickable: plainScroll }

        Text {
            id: plainText

            y: panel.fullscreen ? 8 : 4
            width: parent.width
            text: App.lyrics.plain
            wrapMode: Text.Wrap
            font.family: Theme.body.family
            font.pixelSize: panel.fullscreen ? 20 : 14
            lineHeight: panel.fullscreen ? 1.6 : 1.7
            color: panel.plainColor
        }
    }

    // A centred notice (.lyrics-state).
    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 280)
        spacing: 10
        visible: panel.state === "missing" || panel.state === "instrumental" || panel.state === "failed"
            || panel.state === "unavailable"

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 60
            height: 60
            radius: 30
            color: Qt.alpha(panel.headingColor, 0.12)

            Icon {
                anchors.centerIn: parent
                name: panel.state === "failed" ? "wifi-off" : "music-note"
                size: 26
                color: panel.headingColor
            }
        }

        Item { width: 1; height: 6 }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: panel.state === "failed" ? qsTr("Lyrics didn't load")
                : panel.state === "unavailable" ? qsTr("Lyrics need an account")
                : panel.state === "instrumental" ? qsTr("Instrumental")
                : qsTr("No lyrics for this song")
            font.family: Theme.body.family
            font.pixelSize: 19
            font.weight: Font.ExtraBold
            color: panel.headingColor
            Accessible.role: Accessible.StaticText
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: panel.state === "failed" ? qsTr("Check your connection and try again.")
                : panel.state === "unavailable" ? qsTr("Sign in, or add an API key in Settings, to see lyrics.")
                : panel.state === "instrumental" ? qsTr("This song has no vocals. Enjoy the music.")
                : qsTr("We couldn't find lyrics for %1. Enjoy the music.").arg(App.lyrics.songTitle)
            font.family: Theme.body.family
            font.pixelSize: 14
            lineHeight: 1.5
            color: panel.detailColor
        }

        AppButton {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: panel.state === "failed"
            variant: AppButton.Accent
            text: qsTr("Try again")
            onClicked: App.lyrics.retry()
        }
    }
}
