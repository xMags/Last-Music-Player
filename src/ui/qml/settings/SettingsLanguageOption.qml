import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One language in the checklist (.settings-language-option, views.css
// 1503-1556): at least 36px high, padded 6px 8px, a hover fill; an 18px box
// with a 1.5px border that fills with the accent and shows a tick when
// chosen; then the name and the native name. A language the rules refuse
// (the eleventh, say) cannot be ticked and, unless it is ticked, fades.
T.AbstractButton {
    id: option

    required property var language

    readonly property bool refused: option.language.refused === true
    readonly property bool faded: refused && !checked

    checked: option.language.checked === true
    implicitHeight: Math.max(36, label.implicitHeight + 2 * 6)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked
    Accessible.name: option.language.name
    onClicked: {
        if (!option.refused || option.checked)
            App.regionLanguages.toggleLanguage(option.language.code)
    }

    HoverHandler {
        cursorShape: option.refused ? Qt.ForbiddenCursor : Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: 6
        color: option.hovered && !option.refused ? Theme.hover : "transparent"
    }

    contentItem: Item {
        // .settings-language-box
        Rectangle {
            id: box

            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 5
            opacity: option.faded ? 0.45 : 1
            color: option.checked ? Theme.accentColor : "transparent"
            border.width: 1.5
            border.color: option.checked ? Theme.accentColor : Theme.mixSrgb(Theme.fg, "transparent", 0.4)

            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.settingsFade; easing: Motion.ease }
            }

            // ::after: an 8px by 4px corner, 2px thick, turned -45 degrees
            // and lifted 1px.
            Item {
                anchors.centerIn: parent
                width: 8
                height: 4
                visible: option.checked
                rotation: -45
                transform: Translate { y: -1 }

                Rectangle {
                    width: 2
                    height: parent.height
                    color: Theme.accentInk
                }

                Rectangle {
                    y: parent.height - 2
                    width: parent.width
                    height: 2
                    color: Theme.accentInk
                }
            }

            FocusRing {
                shown: option.visualFocus
                controlRadius: 5
                offset: 2
            }
        }

        // .settings-language-text: the names wrap, 8px apart.
        Flow {
            id: label

            anchors.left: box.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8
            opacity: option.faded ? 0.45 : 1

            AppText {
                text: option.language.name
                font: Theme.settingsLanguageText
                lineHeight: Theme.lineHeightSettingsLanguageText
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
            }

            AppText {
                visible: option.language.nativeName !== ""
                height: Theme.lineHeightSettingsLanguageText
                verticalAlignment: Text.AlignBottom
                text: option.language.nativeName
                font: Theme.settingsLanguageNative
                color: Theme.fgTertiary
            }
        }
    }
}
