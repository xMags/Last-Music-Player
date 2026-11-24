pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// A local collection's page (App.collection): the shared collection header,
// the current app's detail toolbar (find, sort, density), then the songs in
// the saved List or Grid layout. In List, Ctrl and Shift clicks select songs
// and a bar at the bottom acts on them; a user playlist in its own order,
// unfiltered, can be put in a new order by dragging its rows. The page
// fills the panel and its list scrolls itself.
Item {
    id: page

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane
    readonly property CollectionViewModel model: App.collection
    property Flickable currentView: songs.view

    ScrollMemory {
        target: page.currentView
    }

    Component {
        id: top

        Column {
            width: parent ? parent.width : 0

            CollectionHeader {
                width: parent.width
                backLabel: page.model.backLabel
                eyebrow: page.model.eyebrow
                title: page.model.title
                description: page.model.description
                byline: page.model.byline
                info: page.model.info
                canPlay: page.model.songCount > 0
                // Share and Download whenever there are songs (library.ts
                // 505-511); More only on what can be renamed or deleted.
                showShare: App.share.available && page.model.songCount > 0
                showDownload: App.capabilities.downloadSongs && page.model.songCount > 0
                showMenu: page.model.userPlaylist || page.model.kind === "album-collection"
                menuWithoutSongs: true
                coverStyle: page.model.coverStyle
                artworkUrl: page.model.artworkUrl
                artworkId: page.model.artworkId
                artworkLabel: page.model.artworkLabel
                collage: page.model.collage
                gradientStart: page.model.gradientStart
                gradientEnd: page.model.gradientEnd
                iconName: page.model.iconName
                onBackClicked: App.navigator.back()
                onPlayClicked: page.model.play()
                onShuffleClicked: page.model.shuffle()
                onShareClicked: page.model.share()
                onDownloadClicked: page.model.download()
                onMenuClicked: at => {
                    // "Playlist options": Rename and Delete only.
                    moreItems.clear()
                    if (page.model.userPlaylist) {
                        moreItems.addItem("rename", qsTr("Rename playlist"))
                        moreItems.addItem("delete", qsTr("Delete playlist"), "trash", true)
                    } else if (page.model.kind === "album-collection") {
                        moreItems.addItem("delete", qsTr("Delete album"), "trash", true)
                    }
                    const place = mapToItem(page, at.x, at.y)
                    moreMenu.openAt(place.x - 20, place.y + 4)
                }
            }

            // The detail toolbar: find, sort and density.
            RowLayout {
                width: parent.width
                spacing: 10
                visible: page.model.songCount > 0 || page.model.findText !== ""

                SearchField {
                    id: find

                    Layout.preferredWidth: 280
                    placeholderText: page.model.findPlaceholder
                    text: page.model.findText
                    onTextChanged: findDelay.restart()
                    onQueryCommitted: query => page.model.findText = query

                    // The current app waits a moment after typing.
                    Timer {
                        id: findDelay

                        interval: 250
                        onTriggered: page.model.findText = find.text
                    }
                }

                Item { Layout.fillWidth: true }

                SegmentedControl {
                    compact: true
                    visible: App.settings.trackView === "list"
                    choices: [qsTr("Comfortable"), qsTr("Compact")]
                    currentIndex: page.model.compact ? 1 : 0
                    onActivated: index => page.model.compact = index === 1
                }

                AppSelect {
                    model: page.model.sorts
                    currentIndex: page.model.sort
                    onActivated: index => page.model.sort = index
                }
            }

            Item { width: 1; height: 16 }

            // .empty-state (library.ts 351): the boxed sentence.
            CompactEmptyState {
                width: parent.width
                compact: false
                visible: page.model.tracks.count === 0
                text: page.model.emptyText
            }
        }
    }

    TrackListView {
        id: songs

        anchors.fill: parent
        model: page.model.tracks
        grid: App.settings.trackView !== "list"
        sidePadding: page.gutter
        rightSidePadding: page.rightGutter
        bottomMargin: page.model.selectedCount > 0 ? 96 : 32
        header: top
        selectable: true
        compact: page.model.compact
        reorderable: page.model.canReorder
        onActivated: row => page.model.activate(row, false, false)
        onSelectRequested: (row, extend) => page.model.activate(row, !extend, extend)
        onLikeRequested: row => page.model.like(row)
        onDragStarted: page.model.loadAll()
        onMoveRequested: (from, to) => page.model.moveRow(from, to)
        onMenuRequested: (row, at, fromKeyboard) => {
            page.model.prepareMenu(row)
            menus.open(songs, at.x, at.y, fromKeyboard)
        }
    }

    // The selection bar: how many, then what can be done with them.
    Rectangle {
        id: selectionBar

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        visible: page.model.selectedCount > 0
        width: barRow.implicitWidth + 2 * 16
        height: 52
        radius: 26
        color: Theme.surfaceElev
        border.width: 1
        border.color: Theme.stroke

        BoxShadow {
            z: -1
            anchors.fill: parent
            layers: Theme.shadowFlyout
            radius: parent.radius
        }

        RowLayout {
            id: barRow

            anchors.centerIn: parent
            spacing: 8

            Text {
                Layout.rightMargin: 6
                text: qsTr("%1 selected").arg(page.model.selectedCount)
                font: Theme.collectionInfoStrong
                color: Theme.fg
            }

            AppButton {
                text: qsTr("Add to queue")
                iconName: "queue"
                onClicked: page.model.queueSelection()
            }

            AppButton {
                id: moveButton

                visible: page.model.userPlaylist
                enabled: page.model.moveTargets.length > 0
                text: qsTr("Move to playlist")
                iconName: "playlist-add"
                onClicked: {
                    moveItems.clear()
                    for (const target of page.model.moveTargets)
                        moveItems.addItem(target.id, target.label)
                    moveMenu.openAbove(moveButton)
                }
            }

            AppButton {
                visible: page.model.userPlaylist
                text: qsTr("Remove")
                iconName: "trash"
                variant: AppButton.Danger
                onClicked: page.model.removeSelection()
            }

            IconButton {
                iconName: "close"
                Accessible.name: qsTr("Clear selection")
                onClicked: page.model.clearSelection()
            }
        }
    }

    MenuItemListModel {
        id: moreItems
    }

    ActionMenu {
        id: moreMenu

        items: moreItems
        onTriggered: itemId => {
            if (itemId === "playNext")
                page.model.playNextAll()
            else if (itemId === "addToQueue")
                page.model.addAllToQueue()
            else if (itemId === "rename")
                App.collections.requestRename(page.model.key, page.model.title)
            else if (itemId === "delete")
                App.collections.requestDelete(page.model.kind, page.model.key, page.model.title)
        }
    }

    MenuItemListModel {
        id: moveItems
    }

    ActionMenu {
        id: moveMenu

        items: moveItems
        onTriggered: itemId => page.model.moveSelection(itemId)
    }

    Connections {
        target: page.model

        function onNotice(text) {
            App.toasts.show(text)
        }
    }

    TrackMenus {
        id: menus
    }
}
