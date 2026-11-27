pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The small "Add to playlist" popup (playlist-menu.ts openPlaylistMenu;
// styles.css 1916-1958), on the menu surface and 232px wide: the label, a
// name field with Create for a new playlist, then the newest playlists with
// how many songs each holds. Picking one or creating closes it. Escape and a
// press outside close it too.
T.Popup {
    id: popup

    // {key, title, count}, newest first (App.trackMenu.playlistChoices).
    property var choices: []

    signal picked(string playlistKey)
    signal created(string name)

    function openAt(px, py) {
        x = px
        y = py
        nameField.text = ""
        open()
    }

    function submit() {
        const name = nameField.text.trim()
        if (name === "")
            return
        close()
        created(name)
    }

    margins: 8
    padding: 4
    width: 232
    implicitHeight: column.implicitHeight + topPadding + bottomPadding
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    transformOrigin: T.Popup.TopLeft

    onOpened: nameField.forceActiveFocus()

    enter: Transition {
        enabled: !Motion.reduced
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.menuDuration; easing: Motion.menu }
        NumberAnimation { property: "scale"; from: 0.98; to: 1; duration: Motion.menuDuration; easing: Motion.menu }
    }

    // The menu surface (styles.css 911-922).
    background: Item {
        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowMenu
            radius: Theme.radiusMd
        }

        BackdropBlur {
            source: App.effects.windowBackdrop
            blurPx: 48
            saturate: 1.5
            cornerRadius: Theme.radiusMd
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusMd
            color: Theme.mixSrgb(Theme.surface, "transparent", 0.97)
            border.width: 1
            border.color: Theme.mixSrgb(Theme.fg, "transparent", 0.10)
        }
    }

    contentItem: Column {
        id: column

        Accessible.role: Accessible.Dialog
        Accessible.name: qsTr("Add to playlist")

        // .playlist-menu-label: padded 4px 10px 6px, 10px 700, 0.08em,
        // upper case, tertiary.
        AppText {
            leftPadding: 10
            topPadding: 4
            bottomPadding: 6
            text: qsTr("Add to playlist").toUpperCase()
            font: Theme.playlistMenuLabel
            color: Theme.fgTertiary
        }

        // .playlist-menu-create: 6px apart, padded 0 5px 7px, a hairline and
        // 5px below while playlists follow (2px of padding otherwise).
        Item {
            width: column.width
            implicitHeight: createRow.implicitHeight + (popup.choices.length > 0 ? 7 + 1 + 5 : 2)

            RowLayout {
                id: createRow

                x: 5
                width: parent.width - 2 * 5
                spacing: 6

                // padded 6px 8px, a 1px stroke, radius 6px, on the page
                // colour, 12px.
                T.TextField {
                    id: nameField

                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    topPadding: 6
                    bottomPadding: 6
                    leftPadding: 8
                    rightPadding: 8
                    implicitHeight: contentHeight + topPadding + bottomPadding
                    maximumLength: 80
                    font: Theme.playlistMenuField
                    color: Theme.fg
                    placeholderText: qsTr("New playlist name")
                    placeholderTextColor: Theme.fgTertiary
                    selectionColor: Theme.accentSoft2
                    selectedTextColor: Theme.fg
                    verticalAlignment: TextInput.AlignVCenter
                    onAccepted: popup.submit()

                    background: Rectangle {
                        radius: 6
                        color: Theme.bg
                        border.width: 1
                        border.color: Theme.stroke
                    }
                }

                // .btn.btn-accent, padded 6px 10px, 12px 600.
                T.Button {
                    id: create

                    implicitHeight: 32
                    implicitWidth: createLabel.implicitWidth + 2 * 10
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    text: qsTr("Create")
                    onClicked: popup.submit()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    contentItem: AppText {
                        id: createLabel

                        text: create.text
                        font: Theme.playlistMenuCreate
                        color: Theme.accentInk
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: ButtonSurface {
                        radius: 6
                        color: create.down ? Theme.mixOklab(Theme.accentColor, Theme.fg, 0.78)
                            : create.hovered ? Theme.mixOklab(Theme.accentColor, Theme.fg, 0.88)
                                             : Theme.accentColor
                        focusShown: create.visualFocus
                    }
                }
            }

            Rectangle {
                visible: popup.choices.length > 0
                x: -4
                y: createRow.implicitHeight + 7
                width: parent.width + 8
                height: 1
                color: Theme.stroke
            }
        }

        Repeater {
            model: popup.choices

            // A menu row without an icon; the count 11px tertiary on the right.
            T.AbstractButton {
                id: choice

                required property var modelData

                width: column.width
                implicitHeight: 34
                leftPadding: 10
                rightPadding: 12
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.title
                onClicked: {
                    popup.close()
                    popup.picked(choice.modelData.key)
                }

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                contentItem: RowLayout {
                    spacing: 8

                    AppText {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: choice.modelData.title
                        font: Theme.menuLabel
                        color: Theme.fg
                        elide: Text.ElideRight
                    }

                    AppText {
                        text: choice.modelData.count
                        font: Theme.playlistMenuCount
                        color: Theme.fgTertiary
                    }
                }

                background: Rectangle {
                    radius: 5
                    color: choice.down ? Theme.mixSrgb(Theme.fg, "transparent", 0.06)
                        : choice.hovered ? Theme.hover : "transparent"

                    FocusRing {
                        shown: choice.visualFocus
                        controlRadius: parent.radius
                        offset: -2
                    }
                }
            }
        }
    }
}
