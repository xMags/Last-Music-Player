pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The List / Grid switch at the end of a collection header (.collection-view,
// views.css 549-571): two 34 by 30px icon buttons in a pill; the chosen one
// sits on the surface. It writes the saved song layout (App.settings.trackView).
Rectangle {
    id: root

    implicitWidth: buttons.implicitWidth + 2 * 3
    implicitHeight: 30 + 2 * 3
    radius: height / 2
    color: Qt.alpha(Theme.surface, 0.72)
    border.width: 1
    border.color: Qt.alpha(Theme.fg, 0.10)

    Row {
        id: buttons

        x: 3
        y: 3

        Repeater {
            model: [{ value: "list", label: qsTr("List") }, { value: "grid", label: qsTr("Grid") }]

            T.AbstractButton {
                id: choice

                required property var modelData
                readonly property bool chosen: (App.settings.trackView === "list") === (modelData.value === "list")

                implicitWidth: 34
                implicitHeight: 30
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: modelData.label
                checkable: false
                onClicked: App.settings.trackView = choice.modelData.value

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        name: choice.modelData.value
                        size: 16
                        color: choice.chosen ? Theme.fg : Theme.fgSecondary
                    }
                }

                background: Item {
                    BoxShadow {
                        anchors.fill: parent
                        visible: choice.chosen
                        layers: Theme.shadow
                        radius: 15
                    }

                    ButtonSurface {
                        anchors.fill: parent
                        radius: 15
                        color: choice.chosen ? Theme.surface : "transparent"
                        focusShown: choice.visualFocus
                    }
                }
            }
        }
    }
}
