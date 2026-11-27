pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import LastMusicPlayer

// The left sidebar (index.html 72-93; styles.css 250-406): the brand row, the
// navigation, the playlists section and the profile card. At 899px and below
// it is a 64px icon rail: labels, the section title and the profile text hide
// and the rows centre their icons.
GlassSurface {
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


    // The brand row: 56px high, padding 0 16px; centred when collapsed.
    Row {
        id: brand

        x: sidebar.collapsed ? (sidebar.width - width) / 2 : 16
        height: 56
        spacing: 10

        BrandMark {
            anchors.verticalCenter: parent.verticalCenter
            size: 26
            // The bars ripple while music plays (motion.css 186-202).
            playing: App.playerBar.playing
        }

        AppText {
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
                    count: kind === Route.Offline ? App.offline.waitingCount : 0
                    countLabel: qsTr("%1 downloading").arg(count)
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

                AppText {
                    x: 4
                    y: 32 - 2
                    text: qsTr("Playlists")
                    font: Theme.sidebarSectionLabel
                    lineHeight: Theme.lineHeightSidebarSectionLabel
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fgTertiary
                }

                // .sidebar-add-button: the 14px plus padded 2px, radius 4px,
                // in the tertiary colour, no hover change; the title as a
                // tooltip.
                T.AbstractButton {
                    id: addPlaylist

                    x: parent.width - width - 4
                    y: 32 - 2 + (16 - height) / 2
                    implicitWidth: 14 + 2 * 2
                    implicitHeight: 14 + 2 * 2
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("New playlist")
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("New playlist")
                    ToolTip.delay: 500
                    onClicked: App.collections.requestCreatePlaylist()

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    contentItem: Icon {
                        name: "plus"
                        size: 14
                        color: Theme.fgTertiary
                    }

                    background: Item {
                        FocusRing {
                            shown: addPlaylist.visualFocus
                            controlRadius: 4
                            offset: 1
                        }
                    }
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
                    // The row's own cover properties, filled from the model.
                    required coverStyle
                    required artworkUrl
                    required collage

                    width: column.width
                    visible: index < sidebar.shownPlaylists
                    text: name
                    collapsed: sidebar.collapsed
                    onClicked: App.sidebar.openPlaylist(index)
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
        // Settings, where Sign out also is (main.ts 476-481).
        onClicked: App.navigator.openView(Route.Settings)
    }
}
