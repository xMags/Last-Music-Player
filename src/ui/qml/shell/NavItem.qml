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
                width: Math.min(implicitWidth, control.availableWidth - 20 - control.gap)
                visible: !control.collapsed
                text: control.text
                font: control.active ? Theme.navLabelActive : Theme.navLabel
                color: control.active ? Theme.accentColor : Theme.fg
                elide: Text.ElideRight
            }
        }
    }

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.active ? Theme.accentSoft : control.hovered ? Theme.hover : "transparent"

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.navItemFade; easing: Motion.ease }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: parent.radius
            offset: -2
        }
    }
}
