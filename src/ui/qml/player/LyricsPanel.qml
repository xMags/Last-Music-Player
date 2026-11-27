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
//
// `header` (the rail's playing song) is the first thing in the one scrolling
// area: the synced lines' list holds it as its header, and the other states
// (loading, plain lyrics, a notice) as its footer, so one instance of it
// scrolls away with whatever shows. Without one (the full-screen player) the
// states sit in the panel as they always have.
Item {
    id: panel

    property bool fullscreen: false
    // Scrolls above the lyrics, with them; none by default.
    property Component header: null

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

    // What shows while there are no synced lines: the loading bars, the plain
    // lyrics or a centred notice. `minimum` is the room it fills at least,
    // so a notice is centred in it.
    component StateBody: Item {
        id: body

        property real minimum: 0

        implicitHeight: Math.max(body.minimum,
                                 panel.state === "plain" ? plainText.implicitHeight + 28
                                 : panel.state === "loading" ? bars.y + bars.height
                                 : notice.implicitHeight)

        // Loading: a few soft bars (.lm-skeleton-lyrics).
        Column {
            id: bars

            anchors.left: parent.left
            anchors.right: parent.right
            y: 8
            spacing: 15
            visible: panel.state === "loading"

            Repeater {
                model: [1.0, 0.82, 0.94, 0.7, 0.88, 0.76]

                Skeleton {
                    id: bar

                    required property real modelData

                    width: body.width * bar.modelData
                    height: panel.fullscreen ? 16 : 13
                    radius: height / 2
                }
            }
        }

        // One band for the whole group of lines.
        SkeletonSweep {
            anchors.fill: bars
            visible: bars.visible
            bandColor: Theme.surface
        }

        // Plain lyrics.
        AppText {
            id: plainText

            y: panel.fullscreen ? 8 : 4
            width: parent.width
            visible: panel.state === "plain"
            text: App.lyrics.plain
            wrapMode: Text.Wrap
            font.family: Theme.body.family
            font.pixelSize: panel.fullscreen ? 20 : 14
            lineHeight: panel.fullscreen ? 1.6 : 1.7
            color: panel.plainColor
        }

        // A centred notice (.lyrics-state).
        Column {
            id: notice

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

            AppText {
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

            AppText {
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

    // The scrolling header and the gap under it.
    Component {
        id: headerSlot

        Item {
            width: lines.width
            implicitHeight: headerLoader.implicitHeight > 0 ? headerLoader.implicitHeight + 8 : 0
            height: implicitHeight

            Loader {
                id: headerLoader

                width: parent.width
                sourceComponent: panel.header
            }
        }
    }

    // Under the header, the state that is not synced lines.
    Component {
        id: stateFooter

        StateBody {
            width: lines.width
            minimum: lines.height - lines.topMargin - lines.bottomMargin
                - (lines.headerItem !== null ? lines.headerItem.height : 0)
            height: implicitHeight
        }
    }

    // Synced lines; with a header, every state (see above).
    ListView {
        id: lines

        anchors.fill: parent
        visible: panel.state === "synced" || panel.header !== null
        clip: true
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Lyrics")
        model: panel.state === "synced" ? App.lyrics.lines : []
        header: panel.header !== null ? headerSlot : null
        footer: panel.header !== null && panel.state !== "synced" ? stateFooter : null
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

            AppText {
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

    // Without a header: the other states in the panel itself; plain lyrics
    // scroll on their own.
    Flickable {
        id: stateScroll

        anchors.fill: parent
        visible: panel.header === null && panel.state !== "synced"
        clip: true
        contentHeight: stateBody.height
        boundsBehavior: Flickable.StopAtBounds

        WheelScroll { flickable: stateScroll }

        StateBody {
            id: stateBody

            width: stateScroll.width
            minimum: stateScroll.height
            height: implicitHeight
        }
    }
}
