import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Languages (views/settings-languages.ts): which languages recommendations
// keep to. Automatic shows what was detected from listening, Choose a list
// the listener ticks (saved as they tick), Off every language.
SettingsSection {
    id: section

    readonly property var model: App.regionLanguages
    readonly property var modes: ["auto", "custom", "off"]

    iconName: "music-note"
    title: qsTr("Languages")

    SettingsRow {
        title: qsTr("Recommendation languages")
        description: qsTr("Autoplay, Home and similar artists keep to these. Search, charts, your library and playlists still show everything.")

        SegmentedControl {
            compact: true
            choices: [qsTr("Automatic"), qsTr("Choose"), qsTr("Off")]
            currentIndex: section.modes.indexOf(section.model.mode)
            onActivated: index => section.model.selectMode(section.modes[index])
        }
    }

    // The panel under the switch, by mode.
    ColumnLayout {
        id: panel

        width: parent ? parent.width : 0
        spacing: 10

        Item { Layout.preferredHeight: 2 }

        // Automatic: detected languages, else the region's, then Detect again.
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: section.model.mode === "auto"
            text: section.model.detected.length > 0 ? qsTr("Detected from your listening:")
                : section.model.detecting ? qsTr("Detecting your languages…")
                : section.model.regionLanguages.length > 0
                    ? qsTr("Not enough listening yet. Until then, recommendations keep to your region's languages:")
                    : qsTr("Not enough listening yet. Keep playing and it will learn your languages.")
            font: Theme.settingsRowDescription
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }

        LanguagePills {
            visible: section.model.mode === "auto" && !section.model.detecting
            languages: section.model.detected.length > 0 ? section.model.detected : section.model.regionLanguages
        }

        Text {
            Layout.leftMargin: 20
            visible: section.model.mode === "auto" && section.model.detected.length > 0
                && section.model.detectedAgo !== ""
            text: qsTr("Updated %1").arg(section.model.detectedAgo)
            font: Theme.settingsRowDescription
            color: Theme.fgTertiary
        }

        RowLayout {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: section.model.mode === "auto"
            spacing: 12

            AppButton {
                enabled: !section.model.detecting
                text: section.model.detecting ? qsTr("Detecting…") : qsTr("Detect again")
                onClicked: section.model.detect()
            }

            Text {
                Layout.fillWidth: true
                visible: section.model.detectError !== ""
                text: section.model.detectError
                font: Theme.settingsRowDescription
                color: Theme.danger
                wrapMode: Text.Wrap
            }
        }

        // Choose: the chosen list, then the checklist.
        Text {
            Layout.leftMargin: 20
            visible: section.model.mode === "custom" && section.model.catalogLoading
            text: qsTr("Loading languages…")
            font: Theme.settingsRowDescription
            color: Theme.fgSecondary
        }

        RowLayout {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: section.model.mode === "custom" && section.model.catalogError !== ""
            spacing: 12

            Text {
                text: section.model.catalogError
                font: Theme.settingsRowDescription
                color: Theme.danger
            }

            AppButton {
                text: qsTr("Retry")
                onClicked: section.model.retryCatalog()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: 20
            visible: section.model.mode === "custom" && section.model.chosen.length > 0
            spacing: 12

            LanguagePills {
                Layout.fillWidth: true
                languages: section.model.chosen
            }

            AppButton {
                Layout.alignment: Qt.AlignTop
                text: section.model.editorOpen ? qsTr("Done") : qsTr("Edit")
                onClicked: section.model.toggleEditor()
            }
        }

        Text {
            Layout.leftMargin: 20
            visible: section.model.mode === "custom" && section.model.chosen.length === 0
                && !section.model.catalogLoading
            text: qsTr("No languages chosen yet.")
            font: Theme.settingsRowDescription
            color: Theme.fgSecondary
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: section.model.mode === "custom" && section.model.editorOpen
            spacing: 12

            SearchField {
                Layout.preferredWidth: 280
                placeholderText: qsTr("Search languages")
                text: section.model.search
                onQueryEdited: text => section.model.search = text
            }

            Repeater {
                model: section.model.groups

                delegate: ColumnLayout {
                    id: group

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: group.modelData.label
                        font: Theme.eyebrow
                        color: Theme.fgSecondary
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 8

                        Repeater {
                            model: group.modelData.languages

                            delegate: Chip {
                                id: chip

                                required property var modelData

                                text: chip.modelData.nativeName !== ""
                                    ? chip.modelData.name + " · " + chip.modelData.nativeName
                                    : chip.modelData.name
                                selected: chip.modelData.checked
                                opacity: chip.modelData.refused ? 0.55 : 1
                                onClicked: App.regionLanguages.toggleLanguage(chip.modelData.code)
                            }
                        }
                    }
                }
            }

            Text {
                visible: section.model.groups.length === 0 && section.model.search !== ""
                text: qsTr("No languages match")
                font: Theme.settingsRowDescription
                color: Theme.fgSecondary
            }

            Text {
                visible: section.model.hint !== ""
                text: section.model.hint
                font: Theme.settingsRowDescription
                color: Theme.fgSecondary
            }

            RowLayout {
                Layout.fillWidth: true

                Text {
                    Layout.fillWidth: true
                    text: section.model.countText
                    font: Theme.settingsRowDescription
                    color: Theme.fgTertiary
                }

                Text {
                    text: qsTr("Changes save as you tick")
                    font: Theme.settingsRowDescription
                    color: Theme.fgTertiary
                }
            }
        }

        // Off.
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            visible: section.model.mode === "off"
            text: qsTr("Recommendations include every language.")
            font: Theme.settingsRowDescription
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }

        Item { Layout.preferredHeight: 10 }
    }
}
