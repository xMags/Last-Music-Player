import QtQuick
import LastMusicPlayer

// The full-screen player (views/fullscreen.ts; styles.css 2089-2332), always
// dark: the blurred cover behind everything, the exit button (and the panel
// switch while no side panel is chosen) at the top right, and the stage: the
// player column and, when chosen and the window is wider than 900px, the
// side panel beside it.
//
// The stage is padded 84px 48px 44px (with the custom title strip, its
// height plus 72px at the top); the column and the panel are
// clamp(32px, 5vw, 96px) apart. The column is min(620px, 92vw) wide alone
// and up to 560px beside the panel, which is clamp(360px, 34vw, 500px) wide
// and as tall as the stage up to 832px. The cover is min(380px, 42vh).
Rectangle {
    id: player

    // The height of the custom title strip, 0 with the system frame.
    required property int titleHeight

    readonly property bool panelShown: App.fullscreen.sidePanelShown
    readonly property real stageTop: titleHeight > 0 ? titleHeight + 72 : 84
    readonly property real stageWidth: width - 2 * 48
    readonly property real stageHeight: height - stageTop - 44
    readonly property real gap: Math.max(32, Math.min(96, width * 0.05))
    readonly property real sideWidth: Math.max(360, Math.min(500, width * 0.34))
    readonly property real bodyWidth: panelShown ? Math.max(0, Math.min(560, stageWidth - gap - sideWidth)) : Math.min(620, width * 0.92)

    color: Theme.fullscreenBackground
    clip: true

    Component.onCompleted: body.focusPlay()

    // Nothing behind the player reacts to the pointer.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    FullscreenBackdrop {
        id: backdrop

        anchors.fill: parent
        source: App.playerBar.artworkUrl
        // What the tabs and the side panel blur.
        Component.onCompleted: App.effects.fullscreenBackdrop = backdrop
    }

    // .fs-toolbar: 22px from the top (12px below the custom title strip),
    // 26px from the right, 12px apart.
    Row {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: player.titleHeight > 0 ? player.titleHeight + 12 : 22
        anchors.rightMargin: 26
        spacing: 12
        z: 2

        FullscreenTabs {
            anchors.verticalCenter: parent.verticalCenter
            visible: App.fullscreen.panelFits && !App.fullscreen.lyricsChosen && !App.fullscreen.queueChosen
        }

        FullscreenButton {
            Accessible.name: qsTr("Close player")
            anchors.verticalCenter: parent.verticalCenter
            iconName: "fullscreen-exit"
            onClicked: App.fullscreen.close()
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: player.stageTop + (player.stageHeight - height) / 2
        spacing: player.gap

        FullscreenBody {
            id: body

            anchors.verticalCenter: parent.verticalCenter
            width: player.bodyWidth
            artSize: Math.min(380, player.height * 0.42)
        }

        FullscreenSidePanel {
            anchors.verticalCenter: parent.verticalCenter
            visible: player.panelShown
            width: player.sideWidth
            height: Math.max(0, Math.min(832, player.stageHeight))
        }
    }
}
