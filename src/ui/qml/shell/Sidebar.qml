pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The left sidebar (index.html 72-93; styles.css 250-406): the brand row, the
// navigation, the playlists section and the profile card. At 899px and below
// it is a 64px icon rail: labels, the section title and the profile text hide
// and the rows centre their icons.
Rectangle {
    id: sidebar

    readonly property bool collapsed: App.layout.sidebarCollapsed
    // How many playlists fit below Liked Songs without scrolling (one place
    // goes to "All playlists" when some are left out).
    readonly property int playlistRowHeight: 42 + column.spacing
    readonly property int playlistCount: App.sidebar.playlists.count
    readonly property real playlistsTop: column.y + likedAnchor.y + playlistRowHeight
    readonly property int fitting: Math.max(0, Math.floor((scroller.height - playlistsTop - 8) / playlistRowHeight))
    readonly property bool overflowing: playlistCount > fitting
    readonly property int shownPlaylists: overflowing ? Math.max(0, fitting - 1) : playlistCount
    // The playlist whose menu is open.
    property string menuKey: ""
    property string menuTitle: ""

    color: App.windowChrome.backdropActive ? "transparent" : Theme.surface

    // The brand row: 56px high, padding 0 16px; centred when collapsed.
    Row {
        id: brand

        x: sidebar.collapsed ? (sidebar.width - width) / 2 : 16
        height: 56
        spacing: 10

        BrandMark {
            anchors.verticalCenter: parent.verticalCenter
            size: 26
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: !sidebar.collapsed
            text: AppInfo.displayName
            font: Theme.brandLabel
            color: Theme.fg
        }
    }

    // The scrolling middle: padding 16px 16px 8px (12px 8px 8px collapsed),
    // with no visible scroll bar.
    Flickable {
        id: scroller

        y: brand.height
        width: sidebar.width
        height: profile.y - y
        contentHeight: column.implicitHeight + column.y + (sidebar.collapsed ? 8 : 8)
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        WheelScroll { flickable: scroller }

        Column {
            id: column

            // Rows keep 2px above and below; neighbouring margins collapse
            // into one 2px gap, as block margins do in the design.
            x: sidebar.collapsed ? 8 : 16
            y: (sidebar.collapsed ? 12 : 16) + 2
            width: scroller.width - 2 * x
            spacing: 2

            Repeater {
                model: App.sidebar.navItems

                NavItem {
                    id: navRow

                    required property int kind
                    required property string label
                    required property string iconName

                    width: column.width
                    text: label
                    active: kind === App.sidebar.activeKind
                    collapsed: sidebar.collapsed
                    leading: Icon {
                        // Offline uses the plain download glyph; the others have nav icons.
                        name: navRow.iconName === "download" ? "download"
                            : navRow.active ? "nav-" + navRow.iconName + "-active" : "nav-" + navRow.iconName
                        size: 20
                        color: navRow.active ? Theme.accentColor : Theme.fg
                    }
                    onClicked: App.sidebar.openView(kind)
                }
            }

            // PLAYLISTS and its "+": margin 32px 0 10px after the nav, padding
            // 0 4px, 16px line. The column's 2px spacing is part of each margin.
            Item {
                width: column.width
                height: (32 - 2) + 16 + (10 - 2)
                visible: !sidebar.collapsed

                Text {
                    x: 4
                    y: 32 - 2
                    text: qsTr("Playlists")
                    font: Theme.sidebarSectionLabel
                    lineHeight: Theme.lineHeightSidebarSectionLabel
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fgTertiary
                }

                IconButton {
                    x: parent.width - width - 4
                    y: 32 - 2 + (16 - height) / 2
                    implicitWidth: 18
                    implicitHeight: 18
                    shape: IconButton.Square
                    iconName: "plus"
                    iconSize: 14
                    Accessible.name: qsTr("New playlist")
                    onClicked: App.collections.requestCreatePlaylist()
                }
            }

            SidebarPlaylistRow {
                id: likedAnchor

                width: column.width
                text: qsTr("Liked Songs")
                liked: true
                collapsed: sidebar.collapsed
                onClicked: App.sidebar.openLiked()
            }

            // Only the playlists that fit without scrolling; when some are
            // left out, "All playlists" takes the last place and opens
            // Library (main.ts fitSidebarPlaylists).
            Repeater {
                model: App.sidebar.playlists

                SidebarPlaylistRow {
                    id: playlistRow

                    required property int index
                    required property string playlistId
                    required property string name

                    width: column.width
                    visible: index < sidebar.shownPlaylists
                    text: name
                    collapsed: sidebar.collapsed
                    onClicked: App.sidebar.openPlaylist(index)

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: eventPoint => {
                            sidebar.menuKey = playlistRow.playlistId
                            sidebar.menuTitle = playlistRow.name
                            const place = playlistRow.mapToItem(sidebar, eventPoint.position.x, eventPoint.position.y)
                            playlistMenu.openAt(place.x, place.y)
                        }
                    }
                }
            }

            SidebarPlaylistRow {
                width: column.width
                visible: sidebar.overflowing
                text: qsTr("All playlists")
                more: true
                collapsed: sidebar.collapsed
                onClicked: App.sidebar.openAllPlaylists()
            }
        }
    }

    ProfileButton {
        id: profile

        y: sidebar.height - height
        width: sidebar.width
        name: App.profile.name
        plan: App.profile.plan
        letter: App.profile.initials
        avatarUrl: App.profile.avatarUrl
        collapsed: sidebar.collapsed
        onClicked: {
            App.profile.prepareMenu()
            profileMenu.openAt(12, profile.y - profileMenu.implicitHeight - 4)
        }
    }

    MenuItemListModel {
        id: playlistMenuItems

        Component.onCompleted: {
            playlistMenuItems.addItem("rename", qsTr("Rename playlist"))
            playlistMenuItems.addItem("delete", qsTr("Delete playlist"), "trash", true)
        }
    }

    ActionMenu {
        id: playlistMenu

        items: playlistMenuItems
        onTriggered: itemId => {
            if (itemId === "rename")
                App.collections.requestRename(sidebar.menuKey, sidebar.menuTitle)
            else if (itemId === "delete")
                App.collections.requestDelete("playlist", sidebar.menuKey, sidebar.menuTitle)
        }
    }

    // Settings, and Sign out while signed in.
    ActionMenu {
        id: profileMenu

        items: App.profile.menu
        onTriggered: itemId => App.profile.triggerMenu(itemId)
    }
}
