import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import LastMusicPlayer

// One row of the sidebar (.nav-item, styles.css 302-336): an icon and a
// label, 42px high. The active row is tinted with the accent and shows its
// filled icon. In the collapsed (icon-only) sidebar the label hides and the
// name shows as a tooltip instead.
T.AbstractButton {
    id: control

    property bool active: false
    property bool collapsed: false
    // The icon on the left; any item can replace it (playlist swatches).
    property Component leading: null
    property int gap: 20
    // .nav-count: a small accent pill at the row's end ("3" songs on their
    // way); hidden at 0 and in the collapsed sidebar.
    property int count: 0
    property string countLabel: ""

    implicitHeight: 42
    leftPadding: collapsed ? 0 : 12
    rightPadding: collapsed ? 0 : 12
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    ToolTip.visible: collapsed && hovered
    ToolTip.text: text
    ToolTip.delay: 500

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Row {
            // Placed, not anchored: an anchor set to undefined leaves the row
            // where the centring put it, so rows laid out while the sidebar
            // was collapsed kept an offset.
            x: control.collapsed ? (parent.width - width) / 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            spacing: control.collapsed ? 0 : control.gap

            Loader {
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: control.leading
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, control.availableWidth - 20 - control.gap
                                - (countPill.visible ? countPill.width + 8 : 0))
                visible: !control.collapsed
                text: control.text
                font: control.active ? Theme.navLabelActive : Theme.navLabel
                color: control.active ? Theme.accentColor : Theme.fg
                elide: Text.ElideRight
            }
        }
    }

    // min-width 20px, 20px high, padded 0 6px, 12px 600 in the accent on
    // the stronger accent tint, fully round.
    Rectangle {
        id: countPill

        parent: control.contentItem
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        visible: control.count > 0 && !control.collapsed
        width: Math.max(20, countText.implicitWidth + 2 * 6)
        height: 20
        radius: 10
        color: Theme.accentSoft2
        Accessible.name: control.countLabel

        Text {
            id: countText

            anchors.centerIn: parent
            text: control.count
            font: Theme.navCount
            color: Theme.accentColor
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.active ? Theme.accentSoft : control.hovered ? Theme.hover : "transparent"

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.navItemFade; easing: Motion.ease }
        }

        PressRipple {
            radius: parent.radius
            surfaceColor: Theme.bg
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
            offset: -2
        }
    }
}
