pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Placeholder song rows while a list loads (styles.css 734-743): rank, cover,
// title and artist lines, album, time and an action dot per row, under one
// sweep. Each row is 55px high with padding 8px 20px and a divider below.
Item {
    id: root

    property int rows: 6

    implicitWidth: 600
    implicitHeight: column.implicitHeight

    Column {
        id: column

        width: root.width

        Repeater {
            model: root.rows

            Item {
                width: column.width
                height: 55

                // grid-template-columns: 40px minmax(0, 1fr) 200px 60px 28px; gap 16px
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 20
                    anchors.rightMargin: 20
                    spacing: 16

                    Item {
                        Layout.preferredWidth: 40
                        Layout.fillHeight: true

                        Skeleton { anchors.centerIn: parent; width: 12; height: 8 }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 12

                        Skeleton { Layout.preferredWidth: 38; Layout.preferredHeight: 38 }

                        Item {
                            id: lines

                            Layout.fillWidth: true
                            Layout.preferredHeight: 11 + 7 + 9

                            Skeleton { width: lines.width * 0.72; height: 11 }
                            Skeleton { y: 18; width: lines.width * 0.48; height: 9 }
                        }
                    }

                    Item {
                        id: album

                        Layout.preferredWidth: 200
                        Layout.fillHeight: true

                        Skeleton { anchors.verticalCenter: parent.verticalCenter; width: album.width * 0.74; height: 10 }
                    }

                    Item {
                        Layout.preferredWidth: 60
                        Layout.fillHeight: true

                        Skeleton { anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; width: 34; height: 9 }
                    }

                    Item {
                        Layout.preferredWidth: 28
                        Layout.fillHeight: true

                        Skeleton { anchors.centerIn: parent; width: 20; height: 20; radius: 10 }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.divider
                }
            }
        }
    }

    SkeletonSweep {
        anchors.fill: parent
    }

    // Content replacing the skeleton cascades in, as a page opening does.
    onVisibleChanged: {
        if (!visible)
            App.cascade.restart()
    }
    Component.onDestruction: App.cascade.restart()
}
