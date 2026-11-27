pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Sound: the equalizer row with its preset and switch, then the ten bands
// (a 96px track each, the knob at the band's gain, the band under it) and
// the preamp with Reset. Shown disabled, with the reason, while this
// version's audio engine cannot apply it (App.equalizer.applies is false),
// so the saved values can still be seen.
SettingsSection {
    id: section

    title: qsTr("Sound")

    SettingsRow {
        title: qsTr("Equalizer")
        description: App.equalizer.unavailableReason !== "" ? App.equalizer.unavailableReason
                                                            : qsTr("Shape the sound with a preset or the bands")

        Row {
            spacing: 16

            SettingsSelect {
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.equalizer.applies
                implicitWidth: Math.max(160, implicitContentWidth + leftPadding + rightPadding)
                model: App.equalizer.presetNames
                currentIndex: App.equalizer.presetIndex
                onActivated: index => App.equalizer.choosePreset(index)
                Accessible.name: qsTr("Equalizer preset")
            }

            SettingsSwitch {
                anchors.verticalCenter: parent.verticalCenter
                enabled: App.equalizer.applies
                checked: false
                Accessible.name: qsTr("Equalizer")
            }
        }
    }

    SettingsBlock {
        keywords: qsTr("Equalizer") + "\n" + qsTr("Preamp")
        topPadding: 14
        bottomPadding: 20

        ColumnLayout {
            width: parent.width
            enabled: App.equalizer.applies
            opacity: enabled ? 1 : Theme.disabledOpacity
            spacing: 18

            // The bands, spread across with 8px at the ends.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                spacing: 0

                Repeater {
                    model: App.equalizer.bands

                    ColumnLayout {
                        id: band

                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        spacing: 8

                        // The track, and the knob at the gain (-12 to +12 dB).
                        // Both fill the band's width, so the bands share the
                        // row (a layout grows no wider than its parts allow).
                        Item {
                            Layout.fillWidth: true
                            implicitWidth: 16
                            implicitHeight: 96
                            Accessible.role: Accessible.Slider
                            Accessible.name: band.modelData.label
                            Accessible.description: band.modelData.valueText

                            readonly property real ratio: (band.modelData.gain + 12) / 24

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 4
                                height: parent.height
                                radius: 2
                                color: SettingsStyle.controlBorder
                            }

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 16
                                height: 16
                                radius: 8
                                color: Theme.accentColor
                                y: (parent.height - height) * (1 - parent.ratio)
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: band.modelData.label
                            font.family: SettingsStyle.family
                            font.pixelSize: 11
                            color: SettingsStyle.description
                        }
                    }
                }
            }

            // Preamp, 64px wide; the slider; the value, 44px; Reset.
            RowLayout {
                Layout.fillWidth: true
                spacing: 16

                Text {
                    Layout.preferredWidth: 64
                    text: qsTr("Preamp")
                    font.family: SettingsStyle.family
                    font.pixelSize: 13
                    color: Theme.fgSecondary
                }

                AppSlider {
                    id: preamp

                    Layout.fillWidth: true
                    from: -12
                    to: 12
                    stepSize: 0.5
                    sourceValue: App.equalizer.preamp
                    trackColor: SettingsStyle.controlBorder
                    Accessible.name: qsTr("Preamp")
                    onMoved: App.equalizer.setPreamp(preamp.value)
                }

                Text {
                    Layout.preferredWidth: 44
                    horizontalAlignment: Text.AlignRight
                    text: App.equalizer.preampText
                    font.family: SettingsStyle.family
                    font.pixelSize: 12
                    color: SettingsStyle.description
                }

                SettingsButton {
                    text: qsTr("Reset")
                    onClicked: App.equalizer.reset()
                }
            }
        }
    }
}
