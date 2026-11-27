pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import LastMusicPlayer

// The browser sign-in over the whole window (auth-gate.ts; auth.css): an
// always-dark stage with a record sleeve, the record spinning behind it and
// a tonearm that swings onto it, then a line for help. The sleeve holds the
// title, tagline, the platforms, a waveform, any error, the light button
// and the status line. This app also runs without an account, so it opens
// from the Account card or the first run and can be closed; the button
// opens the browser, and while the browser is open it reads "Open the
// browser again" and starts a fresh sign-in. A sign-in that succeeds
// switches to Account mode and closes it.
T.Popup {
    id: dialog

    // The gate's fixed colours (auth.css 40-48).
    readonly property color ground: "#0E1416"
    readonly property color sleeve: "#00505E"
    readonly property color ink: "#F4F1EA"
    readonly property color inkSoft: "#DCEBEE"
    readonly property color label: "#CDE7EC"
    readonly property color muted: "#B8C4C6"
    readonly property color link: "#A7EDFF"
    readonly property color accent: "#F0806A"

    readonly property bool waiting: App.source.signingIn
    // At 860px or less the record rises above a full-width sleeve (auth.css
    // 589-683); wider, the 800px deck shows smaller in a short window.
    readonly property bool compact: width <= 860
    readonly property real deckWidth: compact ? Math.min(342, width - 48) : 800
    readonly property real deckScale: compact ? 1 : Math.min(1, (width - 48) / 800, (height - 96) / 640)

    parent: T.Overlay.overlay
    x: 0
    y: 0
    width: parent ? parent.width : 800
    height: parent ? parent.height : 640
    padding: 0
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape

    onAboutToShow: {
        App.source.clearSignInError()
        dialog.playEntrance()
    }

    // The record slides out, the sleeve rises and the tonearm swings onto
    // the grooves each time the screen opens; with motion off every part
    // simply rests in its place.
    function playEntrance() {
        slideIn.stop()
        armIn.stop()
        riseIn.stop()
        if (Motion.reduced) {
            slideShift.x = 0
            slideShift.y = 0
            slideTurn.angle = 0
            arm.opacity = 1
            armTurn.angle = dialog.compact ? 35 : 17
            sleeveBox.opacity = 1
            rise.y = 0
            return
        }
        arm.opacity = 0
        sleeveBox.opacity = 0
        rise.y = 18
        slideIn.start()
        armIn.start()
        riseIn.start()
    }
    // Closing while the browser is open stops waiting for it.
    onClosed: {
        if (App.source.signingIn)
            App.source.cancelSignIn()
    }

    Connections {
        target: App.source

        function onSignInFinished() {
            dialog.close()
        }
    }

    T.Overlay.modal: Item {}

    enter: Transition {
        enabled: !Motion.reduced

        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.modal; easing: Motion.standard }
    }

    // .auth-stage: radial-gradient(60% 55% at 50% 45%, #15262A, ground 70%).
    background: Rectangle {
        color: dialog.ground

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: dialog.width * 0.5
                    centerY: dialog.height * 0.45
                    centerRadius: Math.max(dialog.width * 0.6, dialog.height * 0.55)
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0; color: "#15262A" }
                    GradientStop { position: 0.7; color: dialog.ground }
                }
                PathRectangle { width: dialog.width; height: dialog.height }
            }
        }
    }

    contentItem: Item {
        // Kept from the dialog this replaced: this app runs without an
        // account, so the gate can be left.
        T.Button {
            id: closeButton

            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            z: 5
            implicitWidth: 40
            implicitHeight: 40
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("Close")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close")
            ToolTip.delay: 500
            onClicked: dialog.close()

            HoverHandler { cursorShape: Qt.PointingHandCursor }

            contentItem: Item {
                Icon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 18
                    color: dialog.ink
                }
            }

            // The caption buttons' hover on the dark gate (rgba(244,241,234,.1)).
            background: Rectangle {
                radius: width / 2
                color: closeButton.hovered ? Qt.rgba(244 / 255, 241 / 255, 234 / 255, 0.1) : "transparent"

                FocusRing {
                    shown: closeButton.visualFocus
                    controlRadius: width / 2
                }
            }
        }

        // The deck (36px above the help line), centred.
        Column {
            anchors.centerIn: parent
            spacing: (dialog.compact ? 28 : 36) * dialog.deckScale

            Item {
                id: deck

                width: dialog.deckWidth * dialog.deckScale
                height: deckContent.height * dialog.deckScale

                Item {
                    id: deckContent

                    width: dialog.deckWidth
                    height: dialog.compact ? sleeveBox.y + sleeveBox.height : sleeveBox.height + 40
                    transform: Scale { xScale: dialog.deckScale; yScale: dialog.deckScale }

                    HoverHandler {
                        id: deckHover
                    }

                    // The record: 470px at (250, 25), sliding in from behind the
                    // sleeve, spinning (faster while signing in); pulled 18px out
                    // while the deck is hovered.
                    Item {
                        id: recordSlot

                        readonly property real size: dialog.compact ? 300 : 470

                        x: dialog.compact ? dialog.deckWidth / 2 - 150 : 250
                        y: dialog.compact ? 0 : 25
                        width: 470
                        height: 470

                        transform: [
                            Rotation { id: slideTurn; origin.x: 235; origin.y: 235; angle: 0 },
                            Translate { id: slideShift; x: 0; y: 0 },
                            Scale { xScale: recordSlot.size / 470; yScale: recordSlot.size / 470 }
                        ]

                        SequentialAnimation {
                            id: slideIn

                            // auth-record-slide from the left, or auth-record-rise from below.
                            PropertyAction { target: slideShift; property: "x"; value: dialog.compact ? 0 : -200 }
                            PropertyAction { target: slideShift; property: "y"; value: dialog.compact ? 160 * 470 / 300 : 0 }
                            PropertyAction { target: slideTurn; property: "angle"; value: -60 }
                            PauseAnimation { duration: 350 }
                            ParallelAnimation {
                                NumberAnimation { target: slideShift; property: "x"; to: 0; duration: 1200; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                                NumberAnimation { target: slideShift; property: "y"; to: 0; duration: 1200; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                                NumberAnimation { target: slideTurn; property: "angle"; to: 0; duration: 1200; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                            }
                        }

                        Item {
                            anchors.fill: parent
                            x: deckHover.hovered && !Motion.reduced && !dialog.compact ? 18 : 0

                            Behavior on x {
                                enabled: !Motion.reduced
                                NumberAnimation { duration: 600; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                            }

                            BoxShadow {
                                anchors.fill: record
                                radius: width / 2
                                layers: Theme.shadowSignInRecord
                            }

                            Item {
                                id: record

                                width: parent.width
                                height: parent.height

                                RotationAnimation on rotation {
                                    from: 0
                                    to: 360
                                    duration: dialog.waiting ? 2400 : 8000
                                    loops: Animation.Infinite
                                    running: dialog.visible && !Motion.reduced && !Motion.ambientHeld
                                }

                                // The grooves, the gaps between the tracks and the rim.
                                Shape {
                                    anchors.fill: parent
                                    preferredRendererType: Shape.CurveRenderer

                                    ShapePath {
                                        strokeColor: "transparent"
                                        fillGradient: RadialGradient {
                                            centerX: 235
                                            centerY: 235
                                            centerRadius: 235
                                            focalX: 235
                                            focalY: 235
                                            GradientStop { position: 0; color: "#141414" }
                                            GradientStop { position: 0.475; color: "#141414" }
                                            GradientStop { position: 0.4751; color: "#050505" }
                                            GradientStop { position: 0.483; color: "#050505" }
                                            GradientStop { position: 0.4831; color: "#131313" }
                                            GradientStop { position: 0.6; color: "#131313" }
                                            GradientStop { position: 0.6001; color: "#050505" }
                                            GradientStop { position: 0.608; color: "#050505" }
                                            GradientStop { position: 0.6081; color: "#131313" }
                                            GradientStop { position: 0.74; color: "#131313" }
                                            GradientStop { position: 0.7401; color: "#050505" }
                                            GradientStop { position: 0.748; color: "#050505" }
                                            GradientStop { position: 0.7481; color: "#131313" }
                                            GradientStop { position: 0.965; color: "#131313" }
                                            GradientStop { position: 0.9651; color: "#0A0A0A" }
                                            GradientStop { position: 1; color: "#0A0A0A" }
                                        }
                                        PathAngleArc { centerX: 235; centerY: 235; radiusX: 235; radiusY: 235; sweepAngle: 360 }
                                    }
                                }

                                // The dead wax and the label: LAST MUSIC, the hole, VOL. 01 · 33⅓.
                                Rectangle {
                                    x: 470 * 0.3
                                    y: 470 * 0.3
                                    width: 470 * 0.4
                                    height: width
                                    radius: width / 2
                                    color: "#0B0B0B"

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width * 0.86
                                        height: width
                                        radius: width / 2
                                        color: dialog.accent
                                        border.width: 5
                                        border.color: Qt.tint(dialog.accent, Qt.rgba(14 / 255, 20 / 255, 22 / 255, 0.18))

                                        Column {
                                            anchors.centerIn: parent
                                            spacing: 7

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: "LAST MUSIC"
                                                font: Theme.signInRecordName
                                                color: dialog.ground
                                            }

                                            Rectangle {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                width: 12
                                                height: 12
                                                radius: 6
                                                color: dialog.ground
                                            }

                                            Text {
                                                anchors.horizontalCenter: parent.horizontalCenter
                                                text: "VOL. 01 · 33⅓"
                                                font: Theme.signInRecordVol
                                                color: dialog.ground
                                            }
                                        }
                                    }
                                }
                            }

                            // The light stays still while the record turns under it.
                            Shape {
                                anchors.fill: parent
                                preferredRendererType: Shape.CurveRenderer

                                ShapePath {
                                    strokeColor: "transparent"
                                    fillGradient: ConicalGradient {
                                        centerX: 235
                                        centerY: 235
                                        angle: 90 - 25
                                        GradientStop { position: 0; color: "transparent" }
                                        GradientStop { position: 30 / 360; color: "transparent" }
                                        GradientStop { position: 50 / 360; color: Qt.rgba(1, 1, 1, 0.09) }
                                        GradientStop { position: 72 / 360; color: "transparent" }
                                        GradientStop { position: 210 / 360; color: "transparent" }
                                        GradientStop { position: 230 / 360; color: Qt.rgba(1, 1, 1, 0.06) }
                                        GradientStop { position: 252 / 360; color: "transparent" }
                                        GradientStop { position: 1; color: "transparent" }
                                    }
                                    PathAngleArc { centerX: 235; centerY: 235; radiusX: 235; radiusY: 235; sweepAngle: 360 }
                                }
                            }
                        }
                    }

                    // The tonearm turns about the base's centre: it rests beside
                    // the record, lifts, swings across and lowers onto the
                    // outer grooves once the record is out.
                    Item {
                        id: arm

                        x: dialog.compact ? dialog.deckWidth - 12 - 4 : 767
                        y: dialog.compact ? -30 - 40 : 22
                        z: 3
                        width: 8
                        height: 300
                        opacity: Motion.reduced ? 1 : 0

                        transform: [
                            Scale { origin.x: 4; origin.y: 40; xScale: dialog.compact ? 0.45 : 1; yScale: dialog.compact ? 0.45 : 1 },
                            Rotation { id: armTurn; origin.x: 4; origin.y: 40; angle: dialog.compact ? 35 : 17 }
                        ]

                        SequentialAnimation {
                            id: armIn

                            PropertyAction { target: armTurn; property: "angle"; value: dialog.compact ? -6 : -4 }
                            PauseAnimation { duration: 300 }
                            NumberAnimation { target: arm; property: "opacity"; to: 1; duration: 500 }
                            PauseAnimation { duration: 800 + 440 }
                            ParallelAnimation {
                                NumberAnimation { target: armTurn; property: "angle"; to: dialog.compact ? 35 : 17; duration: 1120; easing.type: Easing.Bezier; easing.bezierCurve: [0.45, 0, 0.25, 1, 1, 1] }
                                SequentialAnimation {
                                    NumberAnimation { target: armBody; property: "scale"; to: 1.025; duration: 200 }
                                    PauseAnimation { duration: 720 }
                                    NumberAnimation { target: armBody; property: "scale"; to: 1; duration: 200 }
                                }
                            }
                        }

                        Item {
                            id: armBody

                            anchors.fill: parent
                            transformOrigin: Item.TopLeft

                            // The counterweight, the tube, the head and the needle.
                            Rectangle {
                                x: -7
                                width: 22
                                height: 30
                                radius: 5
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "#6F797B" }
                                    GradientStop { position: 0.45; color: "#C9D0D2" }
                                    GradientStop { position: 1; color: "#8A9496" }
                                }
                            }

                            Rectangle {
                                x: 1
                                y: 28
                                width: 6
                                height: 244
                                radius: 3
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "#8A9496" }
                                    GradientStop { position: 0.5; color: "#E6EBEC" }
                                    GradientStop { position: 1; color: "#9AA3A6" }
                                }
                            }

                            Rectangle {
                                x: -6
                                y: 266
                                width: 20
                                height: 32
                                topLeftRadius: 3
                                topRightRadius: 3
                                bottomLeftRadius: 5
                                bottomRightRadius: 5
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: "#3F484B" }
                                    GradientStop { position: 0.5; color: "#6F797B" }
                                    GradientStop { position: 1; color: "#3F484B" }
                                }
                            }

                            Rectangle {
                                x: 2
                                y: 296
                                width: 4
                                height: 6
                                bottomLeftRadius: 2
                                bottomRightRadius: 2
                                color: "#C9D0D2"
                            }
                        }
                    }

                    // The arm's base: a 56px metal disc with a dark cap.
                    Rectangle {
                        x: dialog.compact ? dialog.deckWidth - 27 : 743
                        y: dialog.compact ? -45 : 34
                        z: 4
                        width: dialog.compact ? 30 : 56
                        height: width
                        radius: width / 2
                        opacity: arm.opacity
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#DEE4E6" }
                            GradientStop { position: 0.7; color: "#6F797B" }
                        }
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.2)

                        Rectangle {
                            anchors.centerIn: parent
                            width: dialog.compact ? 11 : 20
                            height: width
                            radius: width / 2
                            color: "#3F484B"
                        }
                    }

                    // The sleeve: 500px square or more, padded 36px; rises in.
                    Item {
                        id: sleeveBox

                        readonly property real padTop: dialog.compact ? 26 : 36
                        readonly property real padSide: dialog.compact ? 24 : 36

                        y: dialog.compact ? 150 : 20
                        z: 2
                        width: dialog.compact ? dialog.deckWidth : 500
                        height: Math.max(dialog.compact ? 0 : 500, sleeveColumn.implicitHeight + padTop + padSide)
                        opacity: Motion.reduced ? 1 : 0

                        transform: Translate { id: rise; y: Motion.reduced ? 0 : 18 }

                        ParallelAnimation {
                            id: riseIn

                            NumberAnimation { target: sleeveBox; property: "opacity"; to: 1; duration: 700; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                            NumberAnimation { target: rise; property: "y"; to: 0; duration: 700; easing.type: Easing.Bezier; easing.bezierCurve: [0.2, 0.8, 0.2, 1, 1, 1] }
                        }

                        BoxShadow {
                            anchors.fill: parent
                            layers: Theme.shadowSignInSleeve
                        }

                        Rectangle {
                            anchors.fill: parent
                            color: dialog.sleeve

                            // The 1px light edge along the top (inset 0 1px 0).
                            Rectangle {
                                width: parent.width
                                height: 1
                                color: Qt.rgba(1, 1, 1, 0.08)
                            }
                        }

                        ColumnLayout {
                            id: sleeveColumn

                            x: sleeveBox.padSide
                            y: sleeveBox.padTop
                            width: parent.width - 2 * sleeveBox.padSide
                            height: parent.height - sleeveBox.padTop - sleeveBox.padSide
                            spacing: 0

                            // .auth-meta: VOL. 01 and LOSSLESS.
                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: "VOL. 01"
                                    font: Theme.signInMeta
                                    color: dialog.label
                                }

                                Text {
                                    text: "LOSSLESS"
                                    font: Theme.signInMeta
                                    color: dialog.label
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 26 : 40
                                text: "LAST <font color=\"" + dialog.accent + "\">MUSIC</font>"
                                textFormat: Text.StyledText
                                font: Theme.signInTitle
                                lineHeight: Theme.lineHeightSignInTitle
                                lineHeightMode: Text.FixedHeight
                                color: dialog.ink
                                fontSizeMode: Text.HorizontalFit
                                minimumPixelSize: 28
                            }

                            Text {
                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 12 : 14
                                text: qsTr("Your library, synced across every platform.")
                                font.family: Theme.signInTagline.family
                                font.weight: Theme.signInTagline.weight
                                font.pixelSize: dialog.compact ? 14 : 15
                                lineHeight: Theme.lineHeightSignInTagline
                                lineHeightMode: Text.FixedHeight
                                color: dialog.inkSoft
                                wrapMode: Text.Wrap
                            }

                            // .auth-platforms: four 38px outlined tiles.
                            GridLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 12 : 14
                                columns: 4
                                columnSpacing: dialog.compact ? 6 : 8

                                Repeater {
                                    model: [
                                        { name: "Windows", path: "M2 2h9.5v9.5H2zM12.5 2H22v9.5h-9.5zM2 12.5h9.5V22H2zM12.5 12.5H22V22h-9.5z", stroke: false },
                                        { name: "Web", path: "M22 12a10 10 0 1 1-20 0a10 10 0 1 1 20 0M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20M2 12h20", stroke: true },
                                        { name: "Android", path: "M 17.6 9.48l 1.84 -3.18c .16 -.31 .04 -.69 -.26 -.85a .637 .637 0 0 0 -.83 .22l -1.88 3.24a 11.463 11.463 0 0 0 -8.94 0L 5.65 5.67a .643 .643 0 0 0 -.87 -.2c -.28 .18 -.37 .54 -.22 .83L 6.4 9.48A 10.78 10.78 0 0 0 1 18h 22a 10.78 10.78 0 0 0 -5.4 -8.52zM 7 15.25a 1.25 1.25 0 1 1 0 -2.5 1.25 1.25 0 0 1 0 2.5zm 10 0a 1.25 1.25 0 1 1 0 -2.5 1.25 1.25 0 0 1 0 2.5z", stroke: false },
                                        { name: "Discord", path: "M 20.317 4.3698a 19.7913 19.7913 0 0 0 -4.8851 -1.5152 .0741 .0741 0 0 0 -.0785 .0371c -.211 .3753 -.4447 .8648 -.6083 1.2495 -1.8447 -.2762 -3.68 -.2762 -5.4868 0 -.1636 -.3933 -.4058 -.8742 -.6177 -1.2495a .077 .077 0 0 0 -.0785 -.037 19.7363 19.7363 0 0 0 -4.8852 1.515 .0699 .0699 0 0 0 -.0321 .0277C .5334 9.0458 -.319 13.5799 .0992 18.0578a .0824 .0824 0 0 0 .0312 .0561c 2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a .0777 .0777 0 0 0 .0842 -.0276c .4616 -.6304 .8731 -1.2952 1.226 -1.9942a .076 .076 0 0 0 -.0416 -.1057c -.6528 -.2476 -1.2743 -.5495 -1.8722 -.8923a .077 .077 0 0 1 -.0076 -.1277c .1258 -.0943 .2517 -.1923 .3718 -.2914a .0743 .0743 0 0 1 .0776 -.0105c 3.9278 1.7933 8.18 1.7933 12.0614 0a .0739 .0739 0 0 1 .0785 .0095c .1202 .099 .246 .1981 .3728 .2924a .077 .077 0 0 1 -.0066 .1276 12.2986 12.2986 0 0 1 -1.873 .8914 .0766 .0766 0 0 0 -.0407 .1067c .3604 .698 .7719 1.3628 1.225 1.9932a .076 .076 0 0 0 .0842 .0286c 1.961 -.6067 3.9495 -1.5219 6.0023 -3.0294a .077 .077 0 0 0 .0313 -.0552c .5004 -5.177 -.8382 -9.6739 -3.5485 -13.6604a .061 .061 0 0 0 -.0312 -.0286zM 8.02 15.3312c -1.1825 0 -2.1569 -1.0857 -2.1569 -2.419 0 -1.3332 .9555 -2.4189 2.157 -2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332 -.9555 2.4189 -2.1569 2.4189zm 7.9748 0c -1.1825 0 -2.1569 -1.0857 -2.1569 -2.419 0 -1.3332 .9554 -2.4189 2.1569 -2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332 -.946 2.4189 -2.1568 2.4189Z", stroke: false }
                                    ]

                                    Rectangle {
                                        id: platform

                                        required property var modelData

                                        Layout.fillWidth: true
                                        implicitHeight: dialog.compact ? 56 : 38
                                        radius: 6
                                        color: "transparent"
                                        border.width: 1
                                        border.color: Qt.rgba(244 / 255, 241 / 255, 234 / 255, 0.22)

                                        // Compact: the mark above the name, 11px.
                                        Grid {
                                            anchors.centerIn: parent
                                            columns: dialog.compact ? 1 : 2
                                            spacing: dialog.compact ? 5 : 8
                                            horizontalItemAlignment: Grid.AlignHCenter
                                            verticalItemAlignment: Grid.AlignVCenter

                                            Shape {
                                                width: 16
                                                height: 16
                                                preferredRendererType: Shape.CurveRenderer

                                                ShapePath {
                                                    scale: Qt.size(16 / 24, 16 / 24)
                                                    strokeColor: platform.modelData.stroke ? dialog.ink : "transparent"
                                                    // The 24px icon's 2px stroke at 16px (scale moves points only).
                                                    strokeWidth: platform.modelData.stroke ? 2 * 16 / 24 : 0
                                                    fillColor: platform.modelData.stroke ? "transparent" : dialog.ink
                                                    fillRule: ShapePath.OddEvenFill
                                                    capStyle: ShapePath.RoundCap
                                                    joinStyle: ShapePath.RoundJoin
                                                    PathSvg { path: platform.modelData.path }
                                                }
                                            }

                                            Text {
                                                text: platform.modelData.name
                                                font.family: Theme.signInPlatform.family
                                                font.weight: Theme.signInPlatform.weight
                                                font.pixelSize: dialog.compact ? 11 : 13
                                                color: dialog.ink
                                            }
                                        }
                                    }
                                }
                            }

                            // .auth-wave: 48 bars on a dashed line, the first 17 in
                            // the accent, the playhead between; each bar pulses.
                            Item {
                                id: wave

                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 22 : 30
                                implicitHeight: dialog.compact ? 44 : 56

                                readonly property var bars: [14, 29, 21, 50, 25, 71, 39, 18, 46, 86, 32, 21, 61, 39, 25, 79, 46, 18, 29, 57, 96, 36, 18, 54,
                                    82, 29, 43, 21, 68, 32, 50, 18, 39, 89, 57, 25, 36, 75, 21, 46, 64, 29, 18, 43, 32, 54, 21, 14]
                                readonly property int played: 17

                                Row {
                                    anchors.centerIn: parent
                                    width: parent.width
                                    height: parent.height
                                    id: barRow

                                    readonly property real barWidth: dialog.compact ? 3 : 4

                                    spacing: (width - barWidth * 48) / 47
                                    z: 1

                                    Repeater {
                                        model: 48

                                        Rectangle {
                                            id: bar

                                            required property int index

                                            anchors.verticalCenter: parent.verticalCenter
                                            width: barRow.barWidth
                                            height: wave.height * wave.bars[bar.index] / 100
                                            radius: 2
                                            color: bar.index < 17 ? dialog.accent : Qt.rgba(244 / 255, 241 / 255, 234 / 255, 0.42)
                                            transform: Scale { id: pulse; origin.y: bar.height / 2; yScale: 1 }

                                            // auth-pulse: 1.6s, from half to full height and back,
                                            // each bar's start spread out.
                                            SequentialAnimation {
                                                running: dialog.visible && !Motion.reduced && !Motion.ambientHeld
                                                loops: Animation.Infinite

                                                PauseAnimation { duration: (bar.index * 137) % 1600 }
                                                SequentialAnimation {
                                                    loops: Animation.Infinite
                                                    NumberAnimation { target: pulse; property: "yScale"; from: 0.5; to: 1; duration: 1600; easing.type: Easing.InOutQuad }
                                                    NumberAnimation { target: pulse; property: "yScale"; from: 1; to: 0.5; duration: 1600; easing.type: Easing.InOutQuad }
                                                }
                                            }
                                        }
                                    }
                                }

                                // The dashed line.
                                Shape {
                                    anchors.fill: parent

                                    ShapePath {
                                        strokeColor: Qt.rgba(244 / 255, 241 / 255, 234 / 255, 0.3)
                                        strokeWidth: 1
                                        strokeStyle: ShapePath.DashLine
                                        dashPattern: [3, 3]
                                        fillColor: "transparent"
                                        startX: 0
                                        startY: wave.height / 2
                                        PathLine { x: wave.width; y: wave.height / 2 }
                                    }
                                }

                                // .auth-wave-head: a 12px dot with a 4px halo
                                // around it (box-shadow 0 0 0 4px), drawn as a
                                // larger disc because a border falls inside.
                                Rectangle {
                                    id: waveHead

                                    readonly property real dot: dialog.compact ? 10 : 12
                                    readonly property real halo: dialog.compact ? 3 : 4

                                    z: 2
                                    x: parent.width * (17 - 0.5) / 48 - width / 2
                                    y: wave.height / 2 - height / 2
                                    width: dot + 2 * halo
                                    height: width
                                    radius: width / 2
                                    color: Qt.rgba(244 / 255, 241 / 255, 234 / 255, 0.18)

                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: waveHead.dot
                                        height: width
                                        radius: width / 2
                                        color: dialog.ink
                                    }
                                }
                            }

                            // .auth-err
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.topMargin: 18
                                visible: App.source.signInError !== ""
                                implicitHeight: errorRow.implicitHeight + 2 * 10
                                radius: 6
                                color: Qt.rgba(14 / 255, 20 / 255, 22 / 255, 0.5)

                                RowLayout {
                                    id: errorRow

                                    x: 12
                                    y: 10
                                    width: parent.width - 2 * 12
                                    spacing: 10

                                    Icon {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 1
                                        name: "alert"
                                        size: 16
                                        color: "#FF9F87"
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: App.source.signInError
                                        font: Theme.signInError
                                        lineHeight: Theme.lineHeightSignInError
                                        lineHeightMode: Text.FixedHeight
                                        color: "#FFE1DA"
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }

                            // .auth-btn
                            T.Button {
                                id: submit

                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 22 : 30
                                implicitHeight: dialog.compact ? 54 : 56
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                // BUTTON_TEXT.magneto: idle, then "Open Magneto again"
                                // while the browser is open.
                                text: dialog.waiting ? qsTr("Open Magneto again") : qsTr("Sign in with Magneto")

                                onClicked: App.source.beginSignIn()

                                HoverHandler {
                                    cursorShape: Qt.PointingHandCursor
                                }

                                transform: Translate { y: submit.down ? 1 : 0 }

                                background: Item {
                                    BoxShadow {
                                        anchors.fill: parent
                                        visible: submit.hovered && !submit.down
                                        radius: 6
                                        layers: Theme.shadowSignInButton
                                    }

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 6
                                        color: submit.hovered ? "white" : dialog.ink

                                        FocusRing {
                                            shown: submit.visualFocus
                                            controlRadius: 6
                                            offset: 3
                                        }
                                    }
                                }

                                contentItem: Item {
                                    // The service's mark 12px before the words, the pair centred.
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 12

                                        Image {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 24
                                            height: 22
                                            source: "qrc:/qt/qml/LastMusicPlayer/account/SignInServiceMark.png"
                                            sourceSize: Qt.size(48, 44)
                                            fillMode: Image.PreserveAspectFit
                                            smooth: true
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: submit.text
                                            font: Theme.signInButton
                                            color: dialog.ground
                                        }
                                    }

                                    // .auth-btn-arrow: an arrow up and to the
                                    // right (M7 17 17 7, M8 7h9v9 on 24), 18px.
                                    Shape {
                                        anchors.right: parent.right
                                        anchors.rightMargin: dialog.compact ? 16 : 18
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 18
                                        height: 18
                                        preferredRendererType: Shape.CurveRenderer
                                        transform: Translate {
                                            x: submit.hovered ? 2 : 0
                                            y: submit.hovered ? -2 : 0
                                        }

                                        ShapePath {
                                            strokeColor: dialog.ground
                                            strokeWidth: 2 * 18 / 24
                                            fillColor: "transparent"
                                            capStyle: ShapePath.RoundCap
                                            joinStyle: ShapePath.RoundJoin
                                            scale: Qt.size(18 / 24, 18 / 24)
                                            PathSvg { path: "M7 17 17 7M8 7h9v9" }
                                        }
                                    }
                                }
                            }

                            Item { Layout.fillHeight: true }

                            // .auth-meta-foot: the status (blinking while signing
                            // in) and the copyright, 20px above, 11px.
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: dialog.compact ? 16 : 20
                                spacing: dialog.compact ? 6 : 8

                                Rectangle {
                                    id: statusDot

                                    Layout.preferredWidth: dialog.compact ? 6 : 7
                                    Layout.preferredHeight: Layout.preferredWidth
                                    radius: Layout.preferredWidth / 2
                                    color: dialog.waiting ? dialog.accent : dialog.label

                                    SequentialAnimation on opacity {
                                        running: dialog.waiting && !Motion.reduced && !Motion.ambientHeld
                                        loops: Animation.Infinite
                                        NumberAnimation { to: 0.25; duration: 600; easing.type: Easing.InOutQuad }
                                        NumberAnimation { to: 1; duration: 600; easing.type: Easing.InOutQuad }
                                        onStopped: statusDot.opacity = 1
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: dialog.waiting ? qsTr("WAITING ON BROWSER") : qsTr("INVITE ONLY")
                                    font: Theme.signInLabel
                                    color: dialog.label
                                }

                                Text {
                                    text: "© LAST PROJECTS 2026"
                                    font: Theme.signInLabel
                                    color: dialog.label
                                }
                            }
                        }
                    }
                }
            }

            // .auth-help: 14px at 1.55, muted, at most 460px.
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(460, dialog.width - 48)
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Need help signing in? <a href=\"https://magnetofx.com/\">Contact Last Music Support</a>")
                textFormat: Text.StyledText
                linkColor: dialog.link
                font.family: Theme.signInHelp.family
                font.pixelSize: dialog.compact ? 13 : 14
                color: dialog.muted
                wrapMode: Text.Wrap
                onLinkActivated: link => Qt.openUrlExternally(link)

                HoverHandler {
                    cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
            }
        }
    }
}
