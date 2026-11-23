pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The three music sources, from the current app (MainWindow.xaml 1584-1700)
// drawn in the Settings card style: an accent-tinted icon chip, the name and
// a badge (ACTIVE in the accent with a tick, else READY or SET UP), a line
// about the source and its status beside a dot that is green when the
// source is ready. The active card has an accent border. Picking a card asks
// App.source to switch.
Column {
    id: root

    spacing: 12

    SettingsSectionHead {
        width: root.width
        iconName: "music-note"
        title: qsTr("Music source")
        note: qsTr("One mode at a time. Local files stay playable in all three.")
    }

    RowLayout {
        width: root.width
        spacing: 12

        Repeater {
            model: App.source.cards

            T.AbstractButton {
                id: sourceCard

                required property var modelData

                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                implicitHeight: body.implicitHeight + 2 * 16
                enabled: modelData.enabled
                opacity: enabled ? 1 : 0.5
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.title
                Accessible.checked: modelData.selected

                onClicked: App.source.choose(modelData.mode)

                HoverHandler {
                    cursorShape: sourceCard.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                }

                background: Item {
                    BoxShadow {
                        anchors.fill: surface
                        layers: Theme.shadowCard
                        radius: surface.radius
                    }

                    Rectangle {
                        id: surface

                        anchors.fill: parent
                        radius: Theme.radiusLg
                        color: sourceCard.hovered && !sourceCard.modelData.selected ? Theme.mixSrgb(Theme.rowHover, Theme.surface, 0.6) : Theme.surface
                        border.width: sourceCard.modelData.selected ? 2 : 1
                        border.color: sourceCard.modelData.selected ? Theme.accentColor : Theme.stroke

                        Behavior on border.color {
                            enabled: !Motion.reduced
                            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
                        }
                    }

                    FocusRing {
                        shown: sourceCard.visualFocus
                        controlRadius: Theme.radiusLg
                    }
                }

                contentItem: Item {
                    ColumnLayout {
                        id: body

                        x: 16
                        y: 16
                        width: parent.width - 2 * 16
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            Rectangle {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                radius: 10
                                color: Theme.accentSoft

                                Icon {
                                    anchors.centerIn: parent
                                    name: sourceCard.modelData.iconName
                                    size: 18
                                    color: Theme.accentColor
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: sourceCard.modelData.title
                                font: Theme.settingsSection
                                color: Theme.fg
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.preferredHeight: 22
                                Layout.preferredWidth: badge.implicitWidth + 2 * 9 + (tick.visible ? tick.width + 4 : 0)
                                radius: height / 2
                                color: sourceCard.modelData.selected ? Theme.accentColor : Theme.stroke

                                Row {
                                    anchors.centerIn: parent
                                    spacing: 4

                                    Icon {
                                        id: tick

                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: sourceCard.modelData.selected
                                        name: "check"
                                        size: 12
                                        color: Theme.accentInk
                                    }

                                    Text {
                                        id: badge

                                        anchors.verticalCenter: parent.verticalCenter
                                        text: sourceCard.modelData.badge
                                        font: Theme.profileTier
                                        color: sourceCard.modelData.selected ? Theme.accentInk : Theme.fgSecondary
                                    }
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: sourceCard.modelData.description
                            font: Theme.settingsRowDescription
                            lineHeight: Theme.lineHeightSettingsRowDescription
                            lineHeightMode: Text.FixedHeight
                            color: Theme.fgSecondary
                            wrapMode: Text.Wrap
                        }

                        Item {
                            Layout.fillHeight: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: sourceCard.modelData.statusReady ? Theme.sourceReady : Theme.fgDisabled
                            }

                            Text {
                                Layout.fillWidth: true
                                text: sourceCard.modelData.status
                                font: Theme.settingsRowDescription
                                color: Theme.fgTertiary
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
