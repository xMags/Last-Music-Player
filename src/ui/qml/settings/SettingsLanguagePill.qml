import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One language as a pill (.settings-language-pill, views.css 1428-1458):
// at least 30px high, padded 5px 12px, a 45% accent border round the soft
// accent fill, the name and then the native name in a mix of the accent and
// the secondary text. A removable pill ends in a 22px round button that
// unticks the language, and pads 4px on the right instead.
Rectangle {
    id: pill

    required property var language

    readonly property bool removable: pill.language.removable === true

    implicitWidth: content.implicitWidth + 12 + (removable ? 4 : 12)
    implicitHeight: Math.max(30, content.implicitHeight + 2 * 5)
    radius: height / 2
    color: Theme.accentSoft
    border.width: 1
    border.color: Theme.mixSrgb(Theme.accentColor, "transparent", 0.45)

    Row {
        id: content

        x: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 6

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: pill.language.name
            font: Theme.settingsLanguageText
            lineHeight: Theme.lineHeightSettingsLanguageText
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: pill.language.nativeName !== ""
            text: pill.language.nativeName
            font: Theme.settingsLanguageNative
            color: Theme.mixSrgb(Theme.accentColor, Theme.fgSecondary, 0.6)
        }

        // margin-left: 2px on top of the 6px gap.
        T.AbstractButton {
            id: remove

            anchors.verticalCenter: parent.verticalCenter
            visible: pill.removable
            width: 22 + 2
            height: 22
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.name: qsTr("Remove %1").arg(pill.language.name)
            onClicked: App.regionLanguages.toggleLanguage(pill.language.code)

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            Rectangle {
                x: 2
                width: 22
                height: 22
                radius: 11
                color: remove.hovered ? Theme.mixSrgb(Theme.accentColor, "transparent", 0.22) : "transparent"

                Icon {
                    anchors.centerIn: parent
                    name: "close"
                    size: 12
                    color: remove.hovered ? Theme.fg : Theme.fgSecondary
                }

                FocusRing {
                    shown: remove.visualFocus
                    controlRadius: 11
                    offset: 1
                }
            }
        }
    }
}
