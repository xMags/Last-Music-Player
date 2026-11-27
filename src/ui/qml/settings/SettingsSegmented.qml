pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A row of choices in Settings: a track in the page colour, 8px corners and
// 3px padding, the choices 2px apart, each 32px high and padded 14px, 13px
// text; the chosen one on a lighter face in semibold. `activated` reports
// the index; the owner writes it and binds `currentIndex` back.
Rectangle {
    id: control

    required property var choices
    property int currentIndex: 0

    signal activated(int index)

    implicitWidth: strip.implicitWidth + 2 * 3
    implicitHeight: 32 + 2 * 3
    radius: 8
    color: SettingsStyle.field
    opacity: enabled ? 1 : Theme.disabledOpacity
    Accessible.role: Accessible.PageTabList

    Row {
        id: strip

        x: 3
        y: 3
        spacing: 2

        Repeater {
            model: control.choices

            T.AbstractButton {
                id: choice

                required property int index
                required property var modelData

                readonly property bool selected: choice.index === control.currentIndex

                implicitWidth: label.implicitWidth + 2 * 14
                implicitHeight: 32
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.PageTab
                Accessible.name: String(choice.modelData)
                Accessible.checked: choice.selected

                onClicked: control.activated(choice.index)

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                background: Rectangle {
                    radius: 6
                    color: choice.selected ? SettingsStyle.quiet : "transparent"

                    FocusRing {
                        shown: choice.visualFocus
                        controlRadius: 6
                    }
                }

                contentItem: Text {
                    id: label

                    text: String(choice.modelData)
                    font.family: SettingsStyle.family
                    font.pixelSize: 13
                    font.weight: choice.selected ? Font.DemiBold : Font.Normal
                    color: choice.selected || choice.hovered ? SettingsStyle.title : Theme.fgSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
