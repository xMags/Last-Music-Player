pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// Languages (views/settings-languages.ts renderLanguageSettings; views.css
// 1403-1570): which languages recommendations keep to. Under the mode
// switch a panel (a divider on top, padded 14px 20px 16px, parts 12px
// apart): Automatic shows what was detected from listening and Detect
// again; Choose the chosen languages as pills with Edit, which opens the
// checklist (saved as it is ticked); Off says every language is included.
// Then "Tune your recommendations" opens the welcome flow.
SettingsSection {
    id: section

    readonly property var model: App.regionLanguages
    readonly property var modes: ["auto", "custom", "off"]
    readonly property bool automatic: section.model.mode === "auto"
    readonly property bool choosing: section.model.mode === "custom"
    // Choose with a catalogue to choose from.
    readonly property bool catalogReady: section.choosing && !section.model.catalogLoading
        && section.model.catalogError === "" && !section.model.catalogEmpty

    iconName: "music-note"
    title: qsTr("Languages")

    SettingsRow {
        title: qsTr("Recommendation languages")
        description: qsTr("Autoplay, Home and similar artists keep to these. Search, charts, your library and playlists still show everything.")

        SegmentedControl {
            compact: true
            choices: [qsTr("Automatic"), qsTr("Choose"), qsTr("Off")]
            currentIndex: section.modes.indexOf(section.model.mode)
            Accessible.name: qsTr("Recommendation languages")
            onActivated: index => section.model.selectMode(section.modes[index])
        }
    }

    // .settings-language-panel
    Item {
        width: parent ? parent.width : 0
        implicitHeight: panel.implicitHeight + 1 + 14 + 16

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
        }

        ColumnLayout {
            id: panel

            x: 20
            y: 1 + 14
            width: parent.width - 2 * 20
            spacing: 12

            // --- Automatic ---------------------------------------------
            Lead {
                visible: section.automatic
                busy: section.model.detected.length === 0 && section.model.detecting
                text: section.model.detected.length > 0 ? qsTr("Detected from your listening:")
                    : section.model.detecting ? qsTr("Detecting your languages…")
                    : section.model.regionLanguages.length > 0
                        ? qsTr("Not enough listening yet. Until then, recommendations keep to your region's languages:")
                        : qsTr("Not enough listening yet. Keep playing and it will learn your languages.")
            }

            Pills {
                visible: section.automatic && (section.model.detected.length > 0
                    || (!section.model.detecting && section.model.regionLanguages.length > 0))
                languages: section.model.detected.length > 0 ? section.model.detected : section.model.regionLanguages
                Accessible.name: section.model.detected.length > 0 ? qsTr("Detected languages") : qsTr("Region languages")
            }

            // .settings-language-updated: 12px, the secondary colour.
            Text {
                Layout.fillWidth: true
                visible: section.automatic && section.model.detected.length > 0 && section.model.detectedAgo !== ""
                text: qsTr("Updated %1").arg(section.model.detectedAgo)
                font: Theme.settingsLanguageNative
                color: Theme.fgSecondary
            }

            // .settings-language-actions: Detect again, then its error.
            Flow {
                Layout.fillWidth: true
                visible: section.automatic
                spacing: 12

                SettingsButton {
                    small: true
                    working: section.model.detecting
                    // aria-disabled while it runs: faded, but still focusable.
                    opacity: section.model.detecting ? Theme.disabledOpacity : 1
                    text: section.model.detecting ? qsTr("Detecting…") : qsTr("Detect again")
                    onClicked: {
                        if (!section.model.detecting)
                            section.model.detect()
                    }
                }

                Hint {
                    visible: section.model.detectError !== ""
                    error: true
                    text: section.model.detectError
                }
            }

            // --- Choose ------------------------------------------------
            Lead {
                visible: section.choosing && section.model.catalogLoading
                busy: true
                text: qsTr("Loading languages…")
            }

            Flow {
                Layout.fillWidth: true
                visible: section.choosing && !section.model.catalogLoading && section.model.catalogError !== ""
                spacing: 12

                Hint {
                    error: true
                    text: section.model.catalogError
                }

                SettingsButton {
                    small: true
                    text: qsTr("Retry")
                    onClicked: section.model.retryCatalog()
                }
            }

            Lead {
                visible: section.choosing && !section.model.catalogLoading && section.model.catalogError === ""
                    && section.model.catalogEmpty
                text: qsTr("No languages are available right now.")
            }

            Lead {
                visible: section.catalogReady && section.model.chosen.length === 0
                text: qsTr("No languages chosen yet.")
            }

            // .settings-language-summary: the pills flow, Edit at the end of
            // the line (margin-left: auto), at least 64px wide.
            Item {
                Layout.fillWidth: true
                visible: section.catalogReady && section.model.chosen.length > 0
                implicitHeight: summary.implicitHeight

                Flow {
                    id: summary

                    width: parent.width
                    spacing: 10

                    Repeater {
                        model: section.model.chosen

                        SettingsLanguagePill {
                            required property var modelData

                            language: modelData
                        }
                    }

                    // Holds Edit's place after the last pill: on that line
                    // when it fits, else on a line of its own.
                    Item {
                        id: editSlot

                        width: edit.width
                        // A pill's height, so Edit centres on the line.
                        height: Math.max(edit.height, 30)
                    }
                }

                // margin-left: auto: at the end of the line its place is on.
                SettingsButton {
                    id: edit

                    x: summary.width - width
                    y: editSlot.y + (editSlot.height - height) / 2
                    small: true
                    implicitWidth: Math.max(64, implicitContentWidth + leftPadding + rightPadding)
                    text: section.model.editorOpen ? qsTr("Done") : qsTr("Edit")
                    Accessible.description: qsTr("Chosen languages")
                    onClicked: section.model.toggleEditor()
                }
            }

            // .settings-language-editor: search, groups, hint, foot; 14px
            // apart. An empty Choose keeps it open until a pick.
            ColumnLayout {
                id: editor

                Layout.fillWidth: true
                visible: section.catalogReady && (section.model.editorOpen || section.model.chosen.length === 0)
                spacing: 14

                LanguageSearch {
                    Layout.fillWidth: true
                }

                Repeater {
                    model: section.model.groups

                    // .settings-language-group: the title over the list, 6px.
                    ColumnLayout {
                        id: group

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            text: group.modelData.label.toUpperCase()
                            font: Theme.settingsLanguageGroup
                            lineHeight: Theme.lineHeightSettingsLanguageGroup
                            lineHeightMode: Text.FixedHeight
                            color: Theme.fgTertiary
                        }

                        // .settings-language-list: margin 0 -8px; columns of
                        // at least 190px filling the width, 2px by 12px apart.
                        Grid {
                            id: list

                            // From the editor's width, not the group's: the group
                            // is sized from this list, which would loop.
                            readonly property real span: editor.width + 16
                            readonly property int count: Math.max(1, Math.floor((span + 12) / (190 + 12)))

                            Layout.leftMargin: -8
                            Layout.preferredWidth: span
                            columns: count
                            rowSpacing: 2
                            columnSpacing: 12

                            Repeater {
                                model: group.modelData.languages

                                SettingsLanguageOption {
                                    required property var modelData

                                    width: (list.span - (list.count - 1) * list.columnSpacing) / list.count
                                    language: modelData
                                }
                            }
                        }
                    }
                }

                // .settings-language-empty: 13px, the tertiary colour.
                Text {
                    Layout.fillWidth: true
                    visible: section.model.groups.length === 0
                    text: qsTr("No languages match")
                    font: Theme.settingsLanguageText
                    color: Theme.fgTertiary
                }

                Hint {
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: section.model.hint
                }

                // .settings-language-foot: the count and how it saves, at
                // either end, 12px/17px.
                Flow {
                    Layout.fillWidth: true
                    spacing: 16

                    Text {
                        text: section.model.countText
                        font: Theme.settingsRowDescription
                        lineHeight: Theme.lineHeightSettingsRowDescription
                        lineHeightMode: Text.FixedHeight
                        color: Theme.fgTertiary
                    }

                    Text {
                        width: Math.max(implicitWidth, parent.width - x)
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Changes save as you tick")
                        font: Theme.settingsRowDescription
                        lineHeight: Theme.lineHeightSettingsRowDescription
                        lineHeightMode: Text.FixedHeight
                        color: Theme.fgTertiary
                    }
                }
            }

            // --- Off ---------------------------------------------------
            Lead {
                visible: section.model.mode === "off"
                text: qsTr("Recommendations include every language.")
            }
        }
    }

    SettingsRow {
        title: qsTr("Tune your recommendations")
        description: qsTr("Pick your languages and the artists you like in one go")

        SettingsButton {
            text: qsTr("Start")
            onClicked: App.welcome.openTune()
        }
    }

    // The flow saves on its own; once it closes the section shows what was
    // picked there.
    Connections {
        target: App.welcome

        function onVisibleChanged() {
            if (!App.welcome.visible)
                section.model.refresh()
        }
    }

    // .settings-language-lead: 13px/18px, the secondary colour, a spinner in
    // front while busy, 8px apart.
    component Lead: RowLayout {
        property alias text: leadText.text
        property bool busy: false

        Layout.fillWidth: true
        spacing: 8

        SettingsSpinner {
            visible: parent.busy
        }

        Text {
            id: leadText

            Layout.fillWidth: true
            font: Theme.settingsLanguageText
            lineHeight: Theme.lineHeightSettingsLanguageText
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }
    }

    // .settings-language-pills: a wrapping row, 8px apart.
    component Pills: Flow {
        id: pillFlow

        property var languages: []

        Layout.fillWidth: true
        spacing: 8
        Accessible.role: Accessible.List

        Repeater {
            model: pillFlow.languages

            SettingsLanguagePill {
                required property var modelData

                language: modelData
            }
        }
    }

    // .settings-language-hint: 12px/17px, the tertiary or danger colour.
    component Hint: Text {
        property bool error: false

        font: Theme.settingsRowDescription
        lineHeight: Theme.lineHeightSettingsRowDescription
        lineHeightMode: Text.FixedHeight
        color: error ? Theme.danger : Theme.fgTertiary
        wrapMode: Text.Wrap
    }

    // .settings-language-search: 36px, padded 0 12px, a stroke with a
    // stronger bottom edge that turns to the accent (with a 1px inset line)
    // while typing, the browse icon and a 13px field.
    component LanguageSearch: Rectangle {
        id: field

        implicitHeight: 36
        radius: Theme.radiusSm
        color: Theme.surfaceAlt
        border.width: 1
        border.color: Theme.stroke
        clip: true

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: input.activeFocus ? 2 : 1
            color: input.activeFocus ? Theme.accentColor : Theme.strokeStrong
        }

        Icon {
            id: glyph

            x: 12
            anchors.verticalCenter: parent.verticalCenter
            name: "browse"
            size: 14
            color: Theme.fgTertiary
        }

        T.TextField {
            id: input

            anchors.left: glyph.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            padding: 0
            verticalAlignment: TextInput.AlignVCenter
            font: Theme.settingsLanguageText
            color: Theme.fg
            selectionColor: Theme.accentSoft2
            selectedTextColor: Theme.fg
            placeholderText: qsTr("Search languages")
            placeholderTextColor: Theme.fgTertiary
            text: App.regionLanguages.search
            Accessible.name: qsTr("Search languages")
            onTextEdited: App.regionLanguages.search = text
            // Escape only clears the search here.
            Keys.onEscapePressed: event => {
                if (text === "") {
                    event.accepted = false
                    return
                }
                App.regionLanguages.search = ""
                event.accepted = true
            }
        }
    }
}
