import QtQuick
import QtQuick.Controls
import LastMusicPlayer

// The small mark beside a song's title (download-badge.ts downloadBadge;
// .dl-badge, views.css 2798-2801): 16px; downloaded is the solid accent
// ring with its arrow cut out, downloading the ring filling with progress,
// waiting a quiet turning arc, failed a red alert. Nothing for a song that
// is not on the list. Its label shows on hover.
Item {
    id: mark

    // The song's address (the rows' remoteUrl role).
    property string remoteUrl: ""

    readonly property var info: App.offline.markRevision >= 0 ? App.offline.markOf(remoteUrl) : ({})
    readonly property string state_: info.state !== undefined ? info.state : ""

    implicitWidth: 16
    implicitHeight: 16
    visible: state_ !== ""
    Accessible.role: Accessible.Graphic
    Accessible.name: info.label !== undefined ? info.label : ""

    Icon {
        anchors.centerIn: parent
        visible: mark.state_ === "done"
        name: "download-done"
        size: 16
        color: Theme.accentColor
    }

    DownloadRing {
        anchors.centerIn: parent
        visible: mark.state_ === "downloading" || mark.state_ === "queued"
        size: 16
        percent: mark.state_ === "downloading" ? mark.info.percent : -1
        color: mark.state_ === "queued" ? Theme.fgTertiary : Theme.accentColor
    }

    Icon {
        anchors.centerIn: parent
        visible: mark.state_ === "failed"
        name: "alert"
        size: 16
        color: Theme.danger
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && Accessible.name !== ""
    ToolTip.text: Accessible.name
    ToolTip.delay: 500
}
