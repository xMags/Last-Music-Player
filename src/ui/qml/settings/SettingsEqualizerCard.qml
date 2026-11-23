pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Sound (views/settings-desktop.ts renderDesktopSound; views.css 1380-1401):
// the equalizer row with its preset, switch and expander, and under it the
// ten bands and the preamp. Shown disabled, with the reason, because this
// version's audio engine cannot apply it (App.equalizer.applies is false);
// only the expander works, so the saved values can be seen.
SettingsSection {
    id: section

    property bool panelOpen: false

    iconName: "eq"
    title: qsTr("Sound")

    SettingsRow {
        title: qsTr("Equalizer")
        description: App.equalizer.unavailableReason

        Row {
            spacing: 12

            AppSelect {
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.equalizer.applies
                implicitWidth: Math.max(180, implicitContentWidth + leftPadding + rightPadding)
                model: App.equalizer.presetNames
                currentIndex: App.equalizer.presetIndex
                onActivated: index => App.equalizer.choosePreset(index)
                Accessible.name: qsTr("Equalizer preset")
            }

            AppSwitch {
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.equalizer.applies
                checked: false
                Accessible.name: qsTr("Equalizer")
            }

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "chevron"
                shape: IconButton.Square
                rotation: section.panelOpen ? -90 : 90
                Accessible.name: section.panelOpen ? qsTr("Hide equalizer bands") : qsTr("Show equalizer bands")
                onClicked: section.panelOpen = !section.panelOpen

                Behavior on rotation {
                    enabled: !Motion.reduced
                    NumberAnimation { duration: 200; easing: Motion.ease }
                }
            }
        }
    }

    // .settings-eq-panel: 20px 24px 18px, a divider on top, 18px between parts.
    Rectangle {
        width: parent ? parent.width : 0
        height: panel.implicitHeight + 38
        visible: section.panelOpen
        color: Qt.alpha(Theme.neutral, 0.4)
        radius: Theme.radiusLg

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
        }

        ColumnLayout {
            id: panel

            enabled: App.equalizer.applies
            opacity: enabled ? 1 : Theme.disabledOpacity
            x: 24
            y: 20
            width: parent.width - 48
            spacing: 18

            // .eq-bands: ten equal columns, 4px apart.
            Row {
                id: bandRow

                Layout.fillWidth: true
                spacing: 4

                Repeater {
                    model: App.equalizer.bands

                    // .eq-band: value, a 120px vertical slider, the label.
                    ColumnLayout {
                        id: band

                        required property var modelData
                        required property int index

                        width: (bandRow.width - 9 * bandRow.spacing) / 10
                        spacing: 8

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: band.modelData.valueText
                            font.pixelSize: 11
                            font.features: { "tnum": 1 }
                            color: Theme.fgSecondary
                        }

                        // The track and the level from 0 dB, drawn: the
                        // slider cannot be moved in this version.
                        Item {
                            Layout.alignment: Qt.AlignHCenter
                            implicitWidth: 24
                            implicitHeight: 120
                            Accessible.role: Accessible.Slider
                            Accessible.name: band.modelData.label
                            Accessible.description: band.modelData.valueText

                            readonly property real ratio: (band.modelData.gain + 12) / 24

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 4
                                height: parent.height
                                radius: 2
                                color: Theme.sliderTrack
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 4
                                radius: 2
                                color: Theme.accentColor
                                y: Math.min(parent.height * (1 - parent.ratio), parent.height / 2)
                                height: Math.abs(parent.height * (parent.ratio - 0.5))
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 12
                                height: 12
                                radius: 6
                                color: Theme.accentColor
                                y: parent.height * (1 - parent.ratio) - height / 2
                            }
                        }

                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: band.modelData.label
                            font.pixelSize: 11
                            color: Theme.fgTertiary
                        }
                    }
                }
            }

            // .eq-preamp: label, slider, value, Reset; 14px apart.
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                Text {
                    text: qsTr("Preamp")
                    font.pixelSize: 12
                    color: Theme.fgTertiary
                }

                AppSlider {
                    id: preamp

                    Layout.fillWidth: true
                    from: -12
                    to: 12
                    stepSize: 0.5
                    sourceValue: App.equalizer.preamp
                    Accessible.name: qsTr("Preamp")
                    onMoved: App.equalizer.setPreamp(preamp.value)
                }

                Text {
                    text: App.equalizer.preampText
                    font.pixelSize: 12
                    color: Theme.fgTertiary
                }

                AppButton {
                    text: qsTr("Reset")
                    onClicked: App.equalizer.reset()
                }
            }
        }
    }
}
