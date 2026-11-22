import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One row of an ActionMenu (.action-menu button, styles.css 931-967): a 16px
// icon column, the label, an optional divider above it that spans the menu.
Column {
    id: row

    required property int index
    required property string label
    required property string iconName
    required property bool itemEnabled
    required property bool danger
    required property bool separatorBefore
    property bool focusShown: false

    signal picked()

    // .menu-separator: 1px, margin 4px -4px.
    Item {
        width: row.width
        height: 9
        visible: row.separatorBefore

        Rectangle {
            x: -4
            y: 4
            width: parent.width + 8
            height: 1
            color: Theme.mixSrgb(Theme.fg, "transparent", 0.09)
        }
    }

    T.AbstractButton {
        id: button

        width: row.width
        // min-height 34px; padding 0 12px 0 10px; gap 12px.
        implicitWidth: 10 + 16 + 12 + label.implicitWidth + 12
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 12
        enabled: row.itemEnabled
        hoverEnabled: true
        focusPolicy: Qt.NoFocus

        onClicked: row.picked()

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Row {
            spacing: 12

            Icon {
                id: glyph

                anchors.verticalCenter: parent.verticalCenter
                name: row.iconName
                size: 16
                color: Theme.fgSecondary
            }

            Text {
                id: label

                anchors.verticalCenter: parent.verticalCenter
                width: button.availableWidth - 16 - 12
                text: row.label
                font: Theme.menuLabel
                color: Theme.fg
                elide: Text.ElideRight
            }
        }

        background: Rectangle {
            id: surface

            radius: 5
            color: "transparent"

            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.menuItemFade; easing: Motion.linear }
            }

            FocusRing {
                shown: row.focusShown
                controlRadius: surface.radius
                offset: -2
            }
        }

        opacity: enabled ? 1 : Theme.disabledOpacity

        states: [
            State {
                name: "pressed"
                when: button.enabled && button.down
                PropertyChanges { surface.color: Theme.mixSrgb(Theme.fg, "transparent", 0.06) }
            },
            State {
                name: "dangerHover"
                when: row.danger && button.enabled && button.hovered
                PropertyChanges { surface.color: Theme.mixSrgb(Theme.danger, "transparent", 0.10) }
            },
            State {
                name: "hover"
                when: button.enabled && button.hovered
                PropertyChanges { surface.color: Theme.hover; glyph.color: Theme.fg }
            }
        ]
    }

    // Danger rows are red whatever their state.
    states: State {
        name: "danger"
        when: row.danger
        PropertyChanges { glyph.color: Theme.danger; label.color: Theme.danger }
    }
}
