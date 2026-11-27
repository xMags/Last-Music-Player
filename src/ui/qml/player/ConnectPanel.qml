pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The Connect panel (connect-ui.ts; .connect-panel-*, styles.css 2694-2862):
// the one place to choose where music plays. It holds the right column in
// place of the queue: "Connect" and Close, where it plays now, the other
// devices, the Cast receivers this PC finds (looked for again every 15 s
// while open), and two questions that open their answers.
Item {
    id: panel

    property string helpOpen: ""

    signal closeRequested()

    // A fresh look each time the panel opens (not at start-up, when both
    // rails build one), then every 15 s while it shows.
    onVisibleChanged: {
        if (visible)
            App.cast.scan()
    }

    Timer {
        interval: 15000
        repeat: true
        running: panel.visible
        onTriggered: App.cast.scan()
    }

    // .connect-panel-head: padding 18px 16px 10px 20px, 17px 700.
    RowLayout {
        id: head

        x: 20
        y: 18
        width: parent.width - 20 - 16

        AppText {
            Layout.fillWidth: true
            text: qsTr("Connect")
            font: Theme.connectTitle
            color: Theme.fg
        }

        IconButton {
            implicitWidth: 32
            implicitHeight: 32
            iconName: "close"
            iconSize: 18
            Accessible.name: qsTr("Close")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Close")
            ToolTip.delay: 500
            onClicked: panel.closeRequested()
        }
    }

    // .connect-panel-body: padding 4px 10px 8px, scrolls.
    Flickable {
        id: body

        x: 10
        y: head.y + head.height + 10 + 4
        width: parent.width - 20
        height: help.y - y - 8
        contentHeight: bodyColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        AppScrollBar.vertical: AppScrollBar {}

        WheelScroll { flickable: body }

        Column {
            id: bodyColumn

            width: body.width

            // .connect-current: 72px or more, padded 12px 16px, radius 10,
            // the text colour at 7%, the icon in the accent.
            Rectangle {
                width: parent.width
                height: Math.max(72, currentRow.implicitHeight + 24)
                radius: 10
                color: Theme.mixSrgb(Theme.fg, "transparent", 0.07)

                RowLayout {
                    id: currentRow

                    x: 16
                    width: parent.width - 32
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14

                    Icon {
                        name: App.cast.current.icon !== undefined ? App.cast.current.icon : "logo-desktop"
                        size: 22
                        color: Theme.accentColor
                    }

                    Column {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 2

                        AppText {
                            width: parent.width
                            text: App.cast.current.name !== undefined ? App.cast.current.name : ""
                            font: Theme.connectCurrentName
                            color: Theme.accentColor
                            elide: Text.ElideRight
                        }

                        AppText {
                            width: parent.width
                            visible: text !== ""
                            text: App.cast.current.detail !== undefined ? App.cast.current.detail : ""
                            font: Theme.connectDetail
                            color: Theme.mixSrgb(Theme.accentColor, Theme.fgSecondary, 0.75)
                            elide: Text.ElideRight
                        }
                    }

                    // .connect-current-stop: padded 6px 12px, an accent pill.
                    T.Button {
                        id: stop

                        visible: App.cast.current.kind === "cast"
                        implicitWidth: stopLabel.implicitWidth + 24
                        implicitHeight: stopLabel.implicitHeight + 12
                        hoverEnabled: true
                        onClicked: App.cast.choose("stop")

                        HoverHandler { cursorShape: Qt.PointingHandCursor }

                        contentItem: AppText {
                            id: stopLabel

                            text: qsTr("Stop")
                            font: Theme.connectStop
                            color: Theme.accentColor
                            horizontalAlignment: Text.AlignHCenter
                        }

                        background: ButtonSurface {
                            radius: height / 2
                            border.width: 1
                            border.color: Theme.mixSrgb(Theme.accentColor, "transparent", 0.5)
                            color: stop.hovered ? Theme.mixSrgb(Theme.accentColor, "transparent", 0.12) : "transparent"
                            focusShown: stop.visualFocus
                        }
                    }
                }
            }

            Item { width: 1; height: 6; visible: App.cast.deviceRows.length > 0 }

            Repeater {
                model: App.cast.deviceRows

                DeviceRow {
                    required property var modelData

                    width: bodyColumn.width
                    row: modelData
                }
            }

            PanelNote {
                width: bodyColumn.width
                text: App.cast.connectNote
            }

            // .connect-panel-section: margin 12px 16px 2px, a hairline and
            // 12px above, 11px 650 upper case.
            Item {
                width: parent.width
                height: 12 + 1 + 12 + sectionLabel.implicitHeight + 2
                visible: App.cast.castRows.length > 0 || App.cast.castNote !== ""

                Rectangle {
                    x: 16
                    y: 12
                    width: parent.width - 32
                    height: 1
                    color: Theme.mixSrgb(Theme.fg, "transparent", 0.09)
                }

                AppText {
                    id: sectionLabel

                    x: 16
                    y: 12 + 1 + 12
                    text: qsTr("Cast").toUpperCase()
                    font: Theme.connectSection
                    color: Theme.fgSecondary
                }
            }

            PanelNote {
                width: bodyColumn.width
                text: App.cast.castNote
            }

            Item { width: 1; height: 6; visible: App.cast.castRows.length > 0 }

            Repeater {
                model: App.cast.castRows

                DeviceRow {
                    required property var modelData

                    width: bodyColumn.width
                    row: modelData
                }
            }
        }
    }

    // .connect-panel-help: padding 6px 10px 14px, a hairline above.
    Column {
        id: help

        x: 10
        y: panel.height - height
        width: parent.width - 20
        topPadding: 6
        bottomPadding: 14

        Rectangle {
            x: -10
            width: help.width + 20
            height: 1
            color: Theme.mixSrgb(Theme.fg, "transparent", 0.07)
        }

        HelpRow {
            width: help.width
            topic: "missing"
            title: qsTr("Don't see your device?")
            answer: qsTr("Open Last Music on it and sign in to this account; it shows up here while the app is open. TVs and speakers need to be on the same Wi-Fi as this device.")
        }

        HelpRow {
            width: help.width
            topic: "what"
            title: qsTr("What can I connect to?")
            answer: qsTr("Last Music on your other computers, browsers and Android phones, and Google Cast TVs and speakers. Choosing one moves the music there; the others become remotes.")
        }
    }

    // .connect-panel-row: 56px or more, padded 8px 16px, radius 8, 14px
    // apart; the icon secondary (the text colour on hover).
    component DeviceRow: T.AbstractButton {
        id: deviceRow

        property var row: ({})

        implicitHeight: Math.max(56, rowContent.implicitHeight + 16)
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: row.name
        onClicked: App.cast.choose(deviceRow.row.id)

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        contentItem: Item {
            RowLayout {
                id: rowContent

                x: 16
                width: parent.width - 32
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                Icon {
                    name: deviceRow.row.icon
                    size: 22
                    color: deviceRow.hovered ? Theme.fg : Theme.fgSecondary
                }

                Column {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 2

                    AppText {
                        width: parent.width
                        text: deviceRow.row.name
                        font: Theme.connectRowName
                        color: Theme.fg
                        elide: Text.ElideRight
                    }

                    AppText {
                        width: parent.width
                        visible: text !== ""
                        text: deviceRow.row.detail
                        font: Theme.connectDetail
                        color: Theme.fgSecondary
                        elide: Text.ElideRight
                    }
                }
            }
        }

        background: Rectangle {
            radius: 8
            color: deviceRow.hovered ? Theme.hover : "transparent"

            PressRipple {
                radius: 8
            }

            FocusRing {
                shown: deviceRow.visualFocus
                controlRadius: 8
                offset: -2
            }
        }
    }

    // .connect-panel-note: margin 8px 16px, 12.5px on 17px, secondary.
    component PanelNote: Item {
        id: note

        property string text: ""

        visible: text !== ""
        implicitHeight: noteText.implicitHeight + 16

        AppText {
            id: noteText

            x: 16
            y: 8
            width: note.width - 32
            text: note.text
            font: Theme.connectNote
            lineHeight: 17
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }
    }

    // .connect-help-row: 44px or more, padded 0 16px, 14px 550; its chevron
    // points down, and up while the answer shows (12.5px on 18px).
    component HelpRow: Column {
        id: helpRow

        property string topic: ""
        property string title: ""
        property string answer: ""
        readonly property bool open: panel.helpOpen === topic

        T.AbstractButton {
            id: helpButton

            width: helpRow.width
            implicitHeight: 44
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: helpRow.title
            onClicked: panel.helpOpen = helpRow.open ? "" : helpRow.topic

            HoverHandler { cursorShape: Qt.PointingHandCursor }

            contentItem: Item {
                AppText {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 32 - 16
                    text: helpRow.title
                    font: Theme.connectHelpRow
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Icon {
                    x: parent.width - 16 - width
                    anchors.verticalCenter: parent.verticalCenter
                    name: "chevron"
                    size: 16
                    color: Theme.fgSecondary
                    rotation: helpRow.open ? -90 : 90

                    Behavior on rotation {
                        enabled: !Motion.reduced
                        NumberAnimation { duration: 150 }
                    }
                }
            }

            background: Rectangle {
                radius: 8
                color: helpButton.hovered ? Theme.hover : "transparent"

                FocusRing {
                    shown: helpButton.visualFocus
                    controlRadius: 8
                    offset: -2
                }
            }
        }

        AppText {
            x: 16
            width: helpRow.width - 32
            visible: helpRow.open
            bottomPadding: 8
            text: helpRow.answer
            font: Theme.connectNote
            lineHeight: 18
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }
    }
}
