import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The welcome flow over the whole window (App.welcome): the languages the
// listener listens to, then artists to follow. A new account must finish it;
// opened later it can be closed.
Rectangle {
    id: flow

    color: Theme.bg

    // Swallows clicks so nothing behind the flow reacts.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 56
        anchors.bottomMargin: 32
        anchors.leftMargin: Math.max(32, (parent.width - 880) / 2)
        anchors.rightMargin: Math.max(32, (parent.width - 880) / 2)
        spacing: 0

        Text {
            text: App.welcome.step === "languages" ? qsTr("STEP 1 OF 2") : qsTr("STEP 2 OF 2")
            font: Theme.eyebrow
            color: Theme.fgSecondary
        }

        Text {
            Layout.topMargin: 8
            Layout.fillWidth: true
            text: App.welcome.step === "languages" ? qsTr("What do you listen to?")
                                                   : qsTr("Pick artists you like")
            font: Theme.collectionTitle
            color: Theme.fg
            wrapMode: Text.Wrap
        }

        Text {
            Layout.topMargin: 8
            Layout.fillWidth: true
            text: App.welcome.step === "languages"
                ? qsTr("Choose the languages of the music you like. Home starts from them.")
                : (App.welcome.missing > 0 ? qsTr("Follow %1 more to continue.").arg(App.welcome.missing)
                                           : qsTr("Follow as many as you like."))
            font: Theme.stateText
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }

        Item { Layout.preferredHeight: 24 }

        // Languages: one chip each.
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.welcome.step === "languages"
            clip: true
            contentHeight: chips.implicitHeight

            Flow {
                id: chips

                width: parent.width
                spacing: 10

                Repeater {
                    model: App.welcome.languages

                    delegate: Chip {
                        required property var modelData

                        text: modelData.nativeName !== "" && modelData.nativeName !== modelData.name
                            ? modelData.name + " · " + modelData.nativeName
                            : modelData.name
                        selected: modelData.selected
                        onClicked: App.welcome.toggleLanguage(modelData.code)
                    }
                }
            }
        }

        // Artists: round pictures with a mark on the picked ones.
        GridView {
            id: grid

            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.welcome.step === "artists"
            clip: true
            readonly property int columns: Math.max(2, Math.floor(grid.width / 150))
            cellWidth: grid.width / grid.columns
            cellHeight: grid.cellWidth + 36
            model: App.welcome.artists

            delegate: Item {
                id: cell

                required property var modelData

                width: GridView.view.cellWidth
                height: GridView.view.cellHeight

                ArtTile {
                    id: picture

                    x: 10
                    width: cell.width - 20
                    height: width
                    radius: width / 2
                    source: cell.modelData.artworkUrl
                    artworkId: "artist:" + cell.modelData.title
                    label: cell.modelData.title

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: 30
                        height: 30
                        radius: 15
                        visible: cell.modelData.picked
                        color: Theme.accentColor

                        Icon {
                            anchors.centerIn: parent
                            name: "check"
                            size: 16
                            color: Theme.onPrimary
                        }
                    }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: App.welcome.toggleArtist(cell.modelData.key)
                    }
                }

                Text {
                    anchors.top: picture.bottom
                    anchors.topMargin: 8
                    width: cell.width
                    horizontalAlignment: Text.AlignHCenter
                    text: cell.modelData.title
                    elide: Text.ElideRight
                    font: Theme.cardTitle
                    color: Theme.fg
                }
            }
        }

        Text {
            Layout.topMargin: 12
            Layout.fillWidth: true
            visible: App.welcome.status !== ""
            text: App.welcome.status
            font: Theme.stateText
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }

        RowLayout {
            Layout.topMargin: 16
            Layout.fillWidth: true
            spacing: 10

            AppButton {
                visible: App.welcome.closable
                text: qsTr("Close")
                onClicked: App.welcome.close()
            }

            AppButton {
                visible: App.welcome.step === "artists"
                text: qsTr("Back")
                onClicked: App.welcome.back()
            }

            Item { Layout.fillWidth: true }

            AppButton {
                variant: AppButton.Accent
                enabled: App.welcome.canContinue
                text: App.welcome.step === "languages" ? qsTr("Continue") : qsTr("Finish")
                onClicked: App.welcome.next()
            }
        }
    }
}
