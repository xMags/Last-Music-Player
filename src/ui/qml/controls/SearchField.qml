import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The search box (.search-bar, styles.css 431-473): a 44px rounded field with
// a search icon, the field and the keyboard shortcut chip. The border turns
// accent while the field has focus. (The reference's clear button is a phone
// control.)
//
// `queryEdited` fires on every change. `queryCommitted` fires once typing has
// paused for Motion.searchCommitDelay, or at once on Enter; `submitted` only
// on Enter. Deciding what a query is worth searching for is the caller's job.
FocusScope {
    id: root

    property alias text: input.text
    property string placeholderText: ""
    property string shortcutHint: ""

    signal queryEdited(string text)
    signal queryCommitted(string text)
    signal submitted(string text)

    function clear() {
        input.clear()
        input.forceActiveFocus()
    }

    // Puts the caret in the field (Ctrl+K), with the text selected.
    function focusField() {
        input.forceActiveFocus(Qt.ShortcutFocusReason)
        input.selectAll()
    }

    // Puts the caret after the text (typing handed over from Home's box).
    function focusAtEnd() {
        input.forceActiveFocus(Qt.OtherFocusReason)
        input.cursorPosition = input.text.length
    }

    implicitWidth: 784
    implicitHeight: 44

    Timer {
        id: commitTimer

        interval: Motion.searchCommitDelay
        onTriggered: root.queryCommitted(input.text)
    }

    BoxShadow {
        anchors.fill: parent
        layers: Theme.shadow
        radius: frame.radius
    }

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: Theme.radiusXl
        color: Theme.surface
        border.width: 1
        border.color: input.activeFocus ? Theme.accentColor : Theme.strokeStrong

        Behavior on border.color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }
    }

    // padding: 0 14px 0 18px; gap: 12px
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 14
        spacing: 12

        Icon {
            name: "browse"
            size: 15
            color: Theme.fgTertiary
        }

        TextInput {
            id: input

            Layout.fillWidth: true
            focus: true
            font: Theme.searchInput
            color: Theme.fg
            selectionColor: Theme.accentSoft2
            selectedTextColor: Theme.fg
            clip: true
            verticalAlignment: TextInput.AlignVCenter

            onTextEdited: {
                root.queryEdited(text)
                commitTimer.restart()
            }
            onAccepted: {
                commitTimer.stop()
                root.queryCommitted(text)
                root.submitted(text)
            }

            AppText {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                text: root.placeholderText
                font: input.font
                color: Theme.fgTertiary
                elide: Text.ElideRight
                visible: input.text.length === 0
            }
        }

        // .kbd-chip: 24px high, padding 0 8px.
        Rectangle {
            visible: root.shortcutHint !== ""
            implicitWidth: hint.implicitWidth + 16
            implicitHeight: 24
            radius: Theme.radiusSm
            color: "transparent"
            border.width: 1
            border.color: Theme.strokeStrong

            AppText {
                id: hint

                anchors.centerIn: parent
                text: root.shortcutHint
                font: Theme.shortcutLabel
                color: Theme.fgTertiary
            }
        }
    }
}
