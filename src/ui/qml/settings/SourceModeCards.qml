pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The three music sources, side by side and 12px apart: each a card padded
// 18px with 12px corners and a 1px border, its icon with IN USE across from
// it on the source in use, the name (15px semibold), a line about the
// source and its status beside a dot that is green when the source is
// ready. The source in use has an accent border on a faint accent face.
// Picking a card asks App.source to switch. The Settings group above names
// them; a search shows them when one of them matches.
RowLayout {
    id: root

    readonly property string searchText: {
        let words = qsTr("Music source")
        for (const card of App.source.cards)
            words += "\n" + card.title + "\n" + card.description
        return words
    }

    visible: SettingsStyle.matches(root.searchText)
    spacing: 12

    Repeater {
        model: App.source.cards

        T.AbstractButton {
            id: sourceCard

            required property var modelData

            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.fillHeight: true
            implicitHeight: body.implicitHeight + 2 * 18
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

            background: Rectangle {
                radius: 12
                color: sourceCard.modelData.selected ? SettingsStyle.chosenFill : SettingsStyle.card
                border.width: 1
                border.color: sourceCard.modelData.selected ? Theme.accentColor
                    : (sourceCard.hovered ? SettingsStyle.quietHover : SettingsStyle.quiet)

                Behavior on border.color {
                    enabled: !Motion.reduced
                    ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
                }

                FocusRing {
                    shown: sourceCard.visualFocus
                    controlRadius: 12
                }
            }

            contentItem: Item {
                ColumnLayout {
                    id: body

                    x: 18
                    y: 18
                    width: parent.width - 2 * 18
                    // The full card, so the status lines of all three line
                    // up at the bottom.
                    height: Math.max(implicitHeight, parent.height - 2 * 18)
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true

                        Icon {
                            name: sourceCard.modelData.iconName
                            size: 22
                            color: SettingsStyle.title
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        AppText {
                            visible: sourceCard.modelData.selected
                            text: qsTr("IN USE")
                            font.family: SettingsStyle.family
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            color: Theme.accentColor
                        }
                    }

                    AppText {
                        Layout.fillWidth: true
                        text: sourceCard.modelData.title
                        font.family: SettingsStyle.family
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: SettingsStyle.title
                        elide: Text.ElideRight
                    }

                    AppText {
                        Layout.fillWidth: true
                        text: sourceCard.modelData.description
                        font.family: SettingsStyle.family
                        font.pixelSize: 12
                        lineHeight: 17
                        lineHeightMode: Text.FixedHeight
                        color: SettingsStyle.description
                        wrapMode: Text.Wrap
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: 8
                            Layout.preferredHeight: 8
                            radius: 4
                            color: sourceCard.modelData.statusReady ? Theme.sourceReady : SettingsStyle.off
                        }

                        AppText {
                            Layout.fillWidth: true
                            text: sourceCard.modelData.status
                            font.family: SettingsStyle.family
                            font.pixelSize: 12
                            color: SettingsStyle.description
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
}
