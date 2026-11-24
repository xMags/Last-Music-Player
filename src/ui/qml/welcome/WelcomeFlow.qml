pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The welcome flow over the whole window (App.welcome; welcome-flow.ts and
// welcome.css): a head with the brand, the two steps and the account (or
// Close when opened later); the step's eyebrow, title and sentence; the
// languages grouped as Settings groups them, with the music region and a
// search, or the artists to follow with a search; and a floating footer
// with Back, the picked faces, the hint and the button on. A new account
// must finish it.
Rectangle {
    id: flow

    color: Theme.bg

    // When the step on screen arrived (ms), so artists made later pop in.
    property double stepShownAt: 0
    readonly property real columnWidth: Math.min(width - 2 * 28, 880)

    // The parts of the column rise in: the first at once, the second after
    // 60 ms, the rest after 120 ms; 14px up, 500 ms.
    function riseIn() {
        flow.stepShownAt = Date.now()
        body.contentY = 0
        if (Motion.reduced)
            return
        const parts = column.children
        for (let index = 0; index < parts.length; ++index) {
            const part = parts[index]
            if (!part.visible)
                continue
            riseComponent.createObject(flow, { target: part, delay: index === 0 ? 0 : index === 1 ? 60 : 120 })
        }
    }

    readonly property string shownStep: App.welcome.step
    onShownStepChanged: riseIn()
    Component.onCompleted: riseIn()

    Component {
        id: riseComponent

        SequentialAnimation {
            id: rising

            property Item target
            property int delay: 0
            readonly property Translate shift: Translate { y: 14 }

            running: true
            onStarted: {
                rising.target.transform = rising.shift
                rising.target.opacity = 0
            }
            onFinished: rising.destroy()

            PauseAnimation { duration: rising.delay }
            ParallelAnimation {
                NumberAnimation { target: rising.target; property: "opacity"; from: 0; to: 1; duration: 500; easing: Motion.ease }
                NumberAnimation { target: rising.shift; property: "y"; from: 14; to: 0; duration: 500; easing: Motion.ease }
            }
        }
    }

    // The region changes the suggested group.
    Connections {
        target: App.regionLanguages

        function onRegionsChanged() {
            App.welcome.reloadLanguages()
        }
    }

    // Swallows clicks so nothing behind the flow reacts.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    // .welcome-head: three columns, padded 18px 28px 10px.
    Item {
        id: head

        x: 28
        y: 18
        width: parent.width - 56
        height: 44

        // .welcome-brand: the mark and "Last Music", 15px 700.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10

            BrandMark {
                anchors.verticalCenter: parent.verticalCenter
                size: 28
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Last Music")
                font: Theme.welcomeBrand
                color: Theme.fg
            }
        }

        // .welcome-steps: a pill with the two steps, a line between them.
        Rectangle {
            anchors.centerIn: parent
            width: steps.implicitWidth + 6 + 14
            height: steps.implicitHeight + 10
            radius: height / 2
            color: Theme.mixSrgb(Theme.surface, "transparent", 0.72)
            border.width: 1
            border.color: Theme.stroke

            Row {
                id: steps

                x: 6
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10

                StepMark {
                    number: 1
                    label: qsTr("Languages")
                    state_: App.welcome.step === "languages" ? "current" : "done"
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 22
                    height: 2
                    radius: 1
                    color: App.welcome.step === "artists" ? Theme.accentColor : Theme.strokeStrong
                }

                StepMark {
                    number: 2
                    label: qsTr("Artists")
                    state_: App.welcome.step === "artists" ? "current" : "next"
                }
            }
        }

        // Close when opened later; the account (and Sign out) otherwise.
        AppButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: App.welcome.closable
            variant: AppButton.Subtle
            iconName: "close"
            iconSize: 16
            text: qsTr("Close")
            onClicked: App.welcome.close()
        }

        T.AbstractButton {
            id: account

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !App.welcome.closable
            implicitWidth: 44
            implicitHeight: 44
            hoverEnabled: true
            scale: down ? 0.96 : hovered ? 1.05 : 1
            Accessible.name: qsTr("Account: %1").arg(App.profile.name)
            onClicked: accountMenu.open()

            Behavior on scale {
                enabled: !Motion.reduced
                NumberAnimation { duration: 140; easing: Motion.ease }
            }

            HoverHandler { cursorShape: Qt.PointingHandCursor }

            contentItem: Item {
                Avatar {
                    anchors.centerIn: parent
                    size: 36
                }
            }

            background: Item {}
        }

        T.Popup {
            id: accountMenu

            x: head.width - width
            y: head.height + 8
            padding: 8
            width: 240
            closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

            background: Rectangle {
                radius: 14
                color: Theme.surface
                border.width: 1
                border.color: Theme.stroke

                BoxShadow {
                    anchors.fill: parent
                    z: -1
                    radius: 14
                    layers: Theme.shadowMenu
                }
            }

            contentItem: Column {
                spacing: 6

                Row {
                    spacing: 12
                    padding: 6

                    Avatar {
                        anchors.verticalCenter: parent.verticalCenter
                        size: 44
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            width: 150
                            text: App.profile.name
                            font: Theme.welcomeAccountName
                            color: Theme.fg
                            elide: Text.ElideRight
                        }

                        Text {
                            text: qsTr("Signed in")
                            font: Theme.connectDetail
                            color: Theme.fgSecondary
                        }
                    }
                }

                AppButton {
                    width: parent.width
                    variant: AppButton.Subtle
                    iconName: "logout"
                    iconSize: 16
                    text: qsTr("Sign out")
                    onClicked: {
                        accountMenu.close()
                        App.source.signOut()
                    }
                }
            }
        }
    }

    // .welcome-body: scrolls under the floating footer (132px of room).
    Flickable {
        id: body

        y: head.y + head.height + 10
        width: parent.width
        height: parent.height - y
        contentHeight: column.implicitHeight + 8 + 132
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        AppScrollBar.vertical: AppScrollBar {}

        WheelScroll { flickable: body }

        Column {
            id: column

            x: (body.width - flow.columnWidth) / 2
            y: 8
            width: flow.columnWidth

            // .welcome-intro: centred, 12px apart, padded 28px 0.
            Column {
                width: parent.width
                topPadding: 28
                bottomPadding: 28
                spacing: 12

                // .welcome-eyebrow: an accent-tinted pill, 11px 700, 0.12em.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: eyebrowText.implicitWidth + 24
                    height: eyebrowText.implicitHeight + 10
                    radius: height / 2
                    color: Theme.accentSoft

                    Text {
                        id: eyebrowText

                        anchors.centerIn: parent
                        text: App.welcome.eyebrow.toUpperCase()
                        font: Theme.welcomeEyebrowPill
                        color: Theme.accentColor
                    }
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: App.welcome.title
                    font.family: Theme.welcomeStepTitle.family
                    font.weight: Theme.welcomeStepTitle.weight
                    font.letterSpacing: Theme.welcomeStepTitle.letterSpacing * pixelSizeNow / 46
                    font.pixelSize: pixelSizeNow
                    readonly property real pixelSizeNow: Math.max(34, Math.min(46, flow.width * 0.042))
                    lineHeight: pixelSizeNow * 1.08
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    wrapMode: Text.Wrap
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(540, parent.width)
                    horizontalAlignment: Text.AlignHCenter
                    text: App.welcome.subtitle
                    font: Theme.welcomeStepSub
                    lineHeight: 16 * 1.55
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fgSecondary
                    wrapMode: Text.Wrap
                }
            }

            // .welcome-tools: the region and the search, centred, 12px
            // apart, 28px above what they lead to.
            Flow {
                id: tools

                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width, childrenRect.width)
                spacing: 12

                // .welcome-region: "Music region" and the country.
                Rectangle {
                    visible: App.welcome.step === "languages"
                    width: regionRow.implicitWidth
                    height: 44
                    radius: 22
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.strokeStrong

                    Row {
                        id: regionRow

                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 16
                            rightPadding: 4
                            text: qsTr("Music region")
                            font: Theme.welcomeRegionLabel
                            color: Theme.fgTertiary
                        }

                        SettingsRegionPicker {
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                WelcomeSearch {
                    visible: App.welcome.step === "languages"
                    width: 300
                    placeholder: qsTr("Search languages")
                    text: App.welcome.languageQuery
                    onEdited: value => App.welcome.languageQuery = value
                }

                WelcomeSearch {
                    visible: App.welcome.step === "artists"
                    width: Math.min(column.width, 460)
                    height: 48
                    wide: true
                    placeholder: qsTr("Search for an artist")
                    text: App.welcome.artistQuery
                    onEdited: value => App.welcome.artistQuery = value
                }
            }

            Item { width: 1; height: 28 }

            // Languages.
            Column {
                width: parent.width
                visible: App.welcome.step === "languages"
                spacing: 28

                WelcomeState {
                    visible: App.welcome.languagesFailed
                    text: qsTr("Could not load the languages.")
                    onRetry: App.welcome.reloadLanguages()
                }

                Text {
                    width: parent.width
                    visible: App.welcome.languagesLoading
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Loading languages…")
                    font: Theme.welcomeEmpty
                    color: Theme.fgSecondary
                }

                Text {
                    width: parent.width
                    visible: !App.welcome.languagesLoading && !App.welcome.languagesFailed
                             && App.welcome.languageGroups.length === 0 && App.welcome.languageQuery.trim() !== ""
                    topPadding: 36
                    bottomPadding: 36
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No languages match “%1”.").arg(App.welcome.languageQuery.trim())
                    font: Theme.welcomeEmpty
                    color: Theme.fgSecondary
                }

                Repeater {
                    model: App.welcome.languageGroups

                    LanguageGroup {
                        required property var modelData

                        width: column.width
                        group: modelData
                    }
                }
            }

            // Artists.
            Column {
                id: artistsStep

                width: parent.width
                visible: App.welcome.step === "artists"

                // .welcome-notice: why the minimum is lifted.
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: App.welcome.notice !== ""
                    width: Math.min(parent.width, 620)
                    height: noticeRow.implicitHeight + 28 + 26
                    color: "transparent"

                    Rectangle {
                        width: parent.width
                        height: parent.height - 26
                        radius: 16
                        color: Theme.mixSrgb(Theme.accentColor, Theme.surface, 0.07)
                        border.width: 1
                        border.color: Theme.mixSrgb(Theme.accentColor, Theme.stroke, 0.26)

                        RowLayout {
                            id: noticeRow

                            x: 16
                            y: 14
                            width: parent.width - 32
                            spacing: 12

                            Icon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                name: "info"
                                size: 16
                                color: Theme.accentColor
                            }

                            Text {
                                Layout.fillWidth: true
                                text: App.welcome.notice
                                font: Theme.welcomeNotice
                                lineHeight: 14 * 1.5
                                lineHeightMode: Text.FixedHeight
                                color: Theme.fgSecondary
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                readonly property bool searching: App.welcome.artistQuery.trim() !== ""
                readonly property var shown: searching ? App.welcome.searchResults : App.welcome.artists

                Text {
                    width: parent.width
                    visible: artistsStep.searching && (App.welcome.searchStatus === "error"
                             || (App.welcome.searchStatus === "ready" && App.welcome.searchResults.length === 0))
                    topPadding: 36
                    bottomPadding: 36
                    horizontalAlignment: Text.AlignHCenter
                    text: App.welcome.searchStatus === "error" ? qsTr("The search did not work. Try again.")
                        : qsTr("No artists found for “%1”.").arg(App.welcome.artistQuery.trim())
                    font: Theme.welcomeEmpty
                    color: Theme.fgSecondary
                }

                Text {
                    width: parent.width
                    visible: (artistsStep.searching && App.welcome.searchStatus === "loading")
                             || (!artistsStep.searching && App.welcome.busy && App.welcome.artists.length === 0)
                    topPadding: 36
                    bottomPadding: 36
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Loading artists…")
                    font: Theme.welcomeEmpty
                    color: Theme.fgSecondary
                }

                // .welcome-artists: as many columns as fit at 128px or more,
                // 26px between rows and 12px between columns.
                Flow {
                    id: artistGrid

                    readonly property int columns: Math.max(1, Math.floor((width + 12) / (128 + 12)))
                    readonly property real cellWidth: (width - (columns - 1) * 12) / columns

                    width: parent.width
                    spacing: 12

                    Repeater {
                        model: artistsStep.shown

                        ArtistCard {
                            required property var modelData

                            width: artistGrid.cellWidth
                            artist: modelData
                        }
                    }
                }
            }
        }
    }

    // .welcome-foot: a fade from the page colour, then the floating bar.
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 36 + 64 + 20
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.alpha(Theme.bg, 0) }
            GradientStop { position: 0.78; color: Theme.bg }
        }

        // .welcome-foot-inner: 64px, padded 9px 9px 9px 22px, a pill.
        Rectangle {
            x: (parent.width - width) / 2
            y: 36
            width: flow.columnWidth
            height: 64
            radius: 32
            color: Theme.mixSrgb(Theme.surface, "transparent", 0.84)
            border.width: 1
            border.color: Theme.strokeStrong

            BoxShadow {
                anchors.fill: parent
                z: -1
                radius: 32
                layers: Theme.shadowWelcomeFoot
            }

            RowLayout {
                x: 22
                width: parent.width - 22 - 9
                anchors.verticalCenter: parent.verticalCenter
                spacing: 14

                AppButton {
                    visible: App.welcome.step === "artists"
                    Layout.leftMargin: -12
                    variant: AppButton.Subtle
                    iconName: "chevron"
                    iconSize: 16
                    iconRotation: 180
                    text: qsTr("Back")
                    Accessible.name: qsTr("Back to languages")
                    onClicked: App.welcome.back()
                }

                // .welcome-tray: the newest picks overlapping, "+N" for the
                // rest, and dashed rings for the places still to fill.
                Row {
                    visible: App.welcome.step === "artists"
                    leftPadding: 8
                    spacing: -8

                    Rectangle {
                        visible: App.welcome.trayMore > 0
                        width: 36
                        height: 36
                        radius: 18
                        color: Theme.neutral
                        border.width: 2.5
                        border.color: Theme.surface

                        Text {
                            anchors.centerIn: parent
                            text: "+" + App.welcome.trayMore
                            font: Theme.welcomeTrayMore
                            color: Theme.fgSecondary
                        }
                    }

                    Repeater {
                        model: App.welcome.trayFaces

                        Rectangle {
                            id: face

                            required property var modelData

                            width: 36
                            height: 36
                            radius: 18
                            color: Theme.surface

                            ArtTile {
                                anchors.fill: parent
                                anchors.margins: 2.5
                                radius: width / 2
                                source: face.modelData.artworkUrl
                                artworkId: "artist:" + face.modelData.key
                                label: face.modelData.title
                            }
                        }
                    }

                    Repeater {
                        model: App.welcome.missing

                        Rectangle {
                            width: 36
                            height: 36
                            radius: 18
                            color: "transparent"
                            border.width: 1.5
                            border.color: Theme.fgDisabled
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: App.welcome.hint
                    font: Theme.welcomeHint
                    color: App.welcome.status !== "" ? Theme.danger : Theme.fgSecondary
                    elide: Text.ElideRight
                }

                // .welcome-next: 46px, at least 164px, the accent with a
                // glow; neutral and unpressable until the step is done.
                T.Button {
                    id: next

                    implicitHeight: 46
                    implicitWidth: Math.max(164, nextRow.implicitWidth + 24 + 18)
                    hoverEnabled: true
                    enabled: App.welcome.canContinue
                    onClicked: App.welcome.next()

                    HoverHandler { cursorShape: next.enabled ? Qt.PointingHandCursor : Qt.ForbiddenCursor }

                    contentItem: Item {
                        Row {
                            id: nextRow

                            anchors.centerIn: parent
                            spacing: 6

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: App.welcome.nextLabel
                                font: Theme.welcomeNext
                                color: next.enabled ? Theme.accentInk : Theme.fgDisabled
                            }

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: "chevron"
                                size: 16
                                color: next.enabled ? Theme.accentInk : Theme.fgDisabled
                                transform: Translate { x: next.enabled && next.hovered && !Motion.reduced ? 3 : 0 }
                            }
                        }
                    }

                    background: ButtonSurface {
                        radius: height / 2
                        color: next.enabled ? Theme.accentColor : Theme.neutral
                        focusShown: next.visualFocus
                    }
                }
            }
        }
    }

    // One step of the head: its dot (number, or a check once done) and name.
    component StepMark: Row {
        id: mark

        property int number: 1
        property string label: ""
        property string state_: "next"

        spacing: 8

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 24
            height: 24
            radius: 12
            color: mark.state_ === "next" ? Theme.neutral : Theme.accentColor
            border.width: mark.state_ === "next" ? 1 : 0
            border.color: Theme.strokeStrong

            // The current step's soft 4px ring.
            Rectangle {
                anchors.centerIn: parent
                visible: mark.state_ === "current"
                width: parent.width + 8
                height: parent.height + 8
                radius: width / 2
                color: "transparent"
                border.width: 4
                border.color: Theme.accentSoft
                z: -1
            }

            Text {
                anchors.centerIn: parent
                visible: mark.state_ !== "done"
                text: mark.number
                font: Theme.welcomeStepDot
                color: mark.state_ === "next" ? Theme.fgTertiary : Theme.accentInk
            }

            Icon {
                anchors.centerIn: parent
                visible: mark.state_ === "done"
                name: "check"
                size: 12
                color: Theme.accentInk
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: mark.label
            font: Theme.welcomeStepLabel
            color: mark.state_ === "current" ? Theme.fg : mark.state_ === "done" ? Theme.fgSecondary : Theme.fgTertiary
        }
    }

    // .welcome-search: a 44px pill (48px wide form) with the browse glyph.
    component WelcomeSearch: Rectangle {
        id: search

        property string placeholder: ""
        property alias text: field.text
        property bool wide: false

        signal edited(string value)

        height: 44
        radius: height / 2
        color: Theme.surface
        border.width: 1
        border.color: field.activeFocus ? Theme.accentColor : Theme.strokeStrong

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            visible: field.activeFocus
            color: "transparent"
            border.width: 4
            border.color: Theme.accentSoft
            z: -1
        }

        Icon {
            id: glass

            x: search.wide ? 18 : 16
            anchors.verticalCenter: parent.verticalCenter
            name: "browse"
            size: 15
            color: field.activeFocus ? Theme.accentColor : Theme.fgTertiary
        }

        T.TextField {
            id: field

            x: glass.x + glass.width + 10
            width: search.width - x - (search.wide ? 18 : 16)
            anchors.verticalCenter: parent.verticalCenter
            font: search.wide ? Theme.welcomeSearchWide : Theme.welcomeSearch
            color: Theme.fg
            placeholderText: search.placeholder
            placeholderTextColor: Theme.fgTertiary
            selectionColor: Theme.accentSoft2
            selectedTextColor: Theme.fg
            onTextEdited: search.edited(text)
            background: Item {}
        }
    }

    // A group of languages: its small capital title with a line after it
    // (the suggested group on a raised card in the accent), then chips.
    component LanguageGroup: Rectangle {
        id: languageGroup

        property var group: ({})
        readonly property bool suggested: group.id === "suggested"

        implicitHeight: groupColumn.implicitHeight + (suggested ? 20 + 22 : 0)
        radius: 22
        color: suggested ? Theme.mixSrgb(Theme.surface, "transparent", 0.78) : "transparent"
        border.width: suggested ? 1 : 0
        border.color: Theme.stroke

        Column {
            id: groupColumn

            x: 22
            y: languageGroup.suggested ? 20 : 0
            width: languageGroup.width - 44
            spacing: 14

            RowLayout {
                width: parent.width
                spacing: 14

                Text {
                    text: String(languageGroup.group.label).toUpperCase()
                    font: Theme.welcomeGroupTitle
                    color: languageGroup.suggested ? Theme.accentColor : Theme.fgTertiary
                }

                Rectangle {
                    Layout.fillWidth: true
                    visible: !languageGroup.suggested
                    implicitHeight: 1
                    color: Theme.divider
                }
            }

            Flow {
                width: parent.width
                spacing: 10

                Repeater {
                    model: languageGroup.group.languages

                    LanguageChip {
                        required property var modelData

                        language: modelData
                    }
                }
            }
        }
    }

    // .welcome-chip: 44px, padded 0 18px 0 12px, 15px 600; the round check
    // keeps its place chosen or not; chosen in the accent.
    component LanguageChip: T.AbstractButton {
        id: chip

        property var language: ({})
        readonly property bool chosen: language.selected === true

        implicitHeight: 44
        implicitWidth: chipRow.implicitWidth + 12 + 18
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: language.name
        Accessible.checkable: true
        Accessible.checked: chosen
        scale: down && !Motion.reduced ? 0.97 : 1
        onClicked: App.welcome.toggleLanguage(chip.language.code)

        transform: Translate { y: chip.hovered && !chip.down && !Motion.reduced ? -1 : 0 }

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        contentItem: Item {
            Row {
                id: chipRow

                x: 12
                anchors.verticalCenter: parent.verticalCenter
                spacing: 9

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 20
                    height: 20
                    radius: 10
                    color: chip.chosen ? Theme.accentColor : "transparent"
                    border.width: chip.chosen ? 0 : 1.5
                    border.color: Theme.strokeStrong
                    scale: chip.chosen && !Motion.reduced ? 1.06 : 1

                    Behavior on scale {
                        enabled: !Motion.reduced
                        NumberAnimation { duration: 220; easing.type: Easing.OutBack }
                    }

                    Icon {
                        anchors.centerIn: parent
                        visible: chip.chosen
                        name: "check"
                        size: 12
                        color: Theme.accentInk
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: chip.language.name
                    font: Theme.welcomeChip
                    color: chip.chosen ? Theme.accentColor : Theme.fg
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: chip.language.nativeName !== ""
                    text: chip.language.nativeName
                    font: Theme.welcomeChipNative
                    color: chip.chosen ? Theme.mixSrgb(Theme.accentColor, Theme.fgTertiary, 0.72) : Theme.fgTertiary
                }
            }
        }

        background: ButtonSurface {
            radius: height / 2
            color: chip.chosen ? Theme.mixSrgb(Theme.accentColor, Theme.surface, 0.11) : Theme.surface
            border.width: 1
            border.color: chip.chosen ? Theme.accentColor
                : chip.hovered ? Theme.mixSrgb(Theme.accentColor, Theme.strokeStrong, 0.45) : Theme.strokeStrong
            focusShown: chip.visualFocus
        }
    }

    // .welcome-artist: the round photo (at most 124px), its check on the
    // top right, the name (14px 600, two lines); picked, the photo steps back
    // inside an accent ring and the name turns accent. Lifts 4px on hover.
    component ArtistCard: T.AbstractButton {
        id: card

        property var artist: ({})
        readonly property bool picked: artist.picked === true
        readonly property real artSize: Math.min(width - 8, 124)

        implicitHeight: 8 + artSize + 12 + nameText.implicitHeight + 8 + 26
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: artist.title
        Accessible.checkable: true
        Accessible.checked: picked
        onClicked: App.welcome.toggleArtist(card.artist.key)

        // An artist added while the step shows pops in: from 78%, 460 ms.
        Component.onCompleted: {
            if (!Motion.reduced && Date.now() - flow.stepShownAt > 600)
                pop.start()
        }

        ParallelAnimation {
            id: pop

            NumberAnimation { target: card; property: "opacity"; from: 0; to: 1; duration: 460; easing: Motion.artistPop }
            NumberAnimation { target: card; property: "scale"; from: 0.78; to: 1; duration: 460; easing: Motion.artistPop }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        contentItem: Item {
            Item {
                id: art

                x: (card.width - width) / 2
                y: 8 + (card.hovered && !card.down && !Motion.reduced ? -4 : 0)
                width: card.artSize
                height: card.artSize
                scale: card.down && !Motion.reduced ? 0.96 : 1

                Behavior on y {
                    enabled: !Motion.reduced
                    NumberAnimation { duration: 220; easing: Motion.ease }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -3
                    radius: width / 2
                    visible: card.picked
                    color: Theme.accentColor
                }

                ArtTile {
                    anchors.centerIn: parent
                    width: parent.width
                    height: parent.height
                    radius: width / 2
                    scale: card.picked ? 0.9 : 1
                    opacity: card.artist.pending === true ? 0.78 : 1
                    big: true
                    source: card.artist.artworkUrl
                    artworkId: "artist:" + card.artist.title
                    label: card.artist.title

                    Behavior on scale {
                        enabled: !Motion.reduced
                        NumberAnimation { duration: 220; easing: Motion.ease }
                    }
                }

                Rectangle {
                    x: parent.width - width + 2
                    y: -2
                    width: 30
                    height: 30
                    radius: 15
                    color: Theme.accentColor
                    border.width: 3
                    border.color: Theme.bg
                    scale: card.picked ? 1 : 0

                    Behavior on scale {
                        enabled: !Motion.reduced
                        NumberAnimation { duration: 240; easing.type: Easing.OutBack }
                    }

                    Icon {
                        anchors.centerIn: parent
                        name: "check"
                        size: 16
                        color: Theme.accentInk
                    }
                }
            }

            Text {
                id: nameText

                y: 8 + card.artSize + 12
                width: card.width - 8
                x: 4
                horizontalAlignment: Text.AlignHCenter
                text: card.artist.title
                font: Theme.welcomeArtistName
                color: card.picked ? Theme.accentColor : Theme.fg
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        background: Item {}
    }

    // .welcome-state: a sentence and Try again.
    component WelcomeState: Row {
        id: state

        property string text: ""

        signal retry()

        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 12
        topPadding: 28
        bottomPadding: 28

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: state.text
            font: Theme.welcomeNotice
            color: Theme.fgSecondary
        }

        AppButton {
            text: qsTr("Try again")
            onClicked: state.retry()
        }
    }

    // The account's photo over its initial, ringed in the accent.
    component Avatar: Rectangle {
        id: avatar

        property real size: 36

        width: size
        height: size
        radius: size / 2
        color: Theme.accentFill
        border.width: 0

        Text {
            anchors.centerIn: parent
            text: App.profile.initials
            font: Theme.welcomeBrand
            color: "white"
        }

        ArtTile {
            anchors.fill: parent
            visible: App.profile.avatarUrl.toString() !== ""
            radius: width / 2
            source: App.profile.avatarUrl
            artworkId: "account"
            label: ""
        }
    }
}
