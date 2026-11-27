import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The one-time "Choose your music source" page, shown before the shell on a
// first run (App.firstRun). It uses the Settings source cards: Offline stays
// with "Continue", API key switches and opens Settings for its form, and
// Account opens the browser sign-in, whose success switches and goes on.
Item {
    id: page

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    TitleStrip {
        id: strip

        width: parent.width
        visible: App.windowChrome.custom
        z: 2
    }

    Flickable {
        id: pageScroll

        anchors.fill: parent
        anchors.topMargin: strip.visible ? strip.height : 0
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + 2 * 48)
        boundsBehavior: Flickable.StopAtBounds

        AppScrollBar.vertical: AppScrollBar {}

        WheelScroll { flickable: pageScroll }

        ColumnLayout {
            id: column

            width: Math.min(parent.width - 2 * 32, 900)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(48, (parent.height - implicitHeight) / 2)
            spacing: 24

            BrandMark {
                Layout.alignment: Qt.AlignHCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                AppText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Choose your music source")
                    font: Theme.settingsHeading
                    lineHeight: Theme.lineHeightSettingsHeading
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    wrapMode: Text.Wrap
                }

                AppText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("You can change it any time in Settings.")
                    font: Theme.body
                    color: Theme.fgSecondary
                    wrapMode: Text.Wrap
                }
            }

            SourceModeCards {
                Layout.fillWidth: true
            }

            AppButton {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Continue")
                variant: AppButton.Accent
                onClicked: App.firstRun.finish()
            }
        }
    }

    Connections {
        target: App.source

        function onSignInRequested() {
            signIn.open()
        }
    }

    SignInDialog {
        id: signIn
    }
}
