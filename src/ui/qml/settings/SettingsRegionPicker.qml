pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The music region control (views/settings.ts regionDialog and
// openRegionFlyout; views.css 1207-1209, 1700-1754): a combo button with the
// region's name, which opens the region list as a flyout under it (above it
// when there is no room below, centred in a narrow window), with a search
// field on top. The list loads with the page; while it loads, or after it
// failed, the flyout says so and offers Retry.
T.Button {
    id: control

    // FLYOUT_GAP, FLYOUT_EDGE and FLYOUT_MIN_VIEWPORT.
    readonly property int flyoutGap: 6
    readonly property int flyoutEdge: 12
    readonly property int flyoutMinViewport: 560

    implicitHeight: 32
    // min-width: 180px; padding: 0 11px 0 12px.
    implicitWidth: Math.max(180, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: 12
    rightPadding: 11
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font: Theme.settingsComboLabel
    text: App.regionLanguages.regionLabel
    Accessible.role: Accessible.ComboBox
    Accessible.name: qsTr("Music region")

    onClicked: flyout.open()

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        implicitWidth: label.implicitWidth + 8 + chevron.width
        implicitHeight: label.implicitHeight

        Text {
            id: label

            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: Theme.fg
            elide: Text.ElideRight
        }

        Icon {
            id: chevron

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron"
            size: 14
            rotation: 90
            color: Theme.fgSecondary
        }
    }

    background: ButtonSurface {
        implicitHeight: 32
        radius: Theme.radiusSm
        border.width: 1
        border.color: Theme.strokeStrong
        color: control.down ? Theme.rowHover : control.hovered ? Theme.hover : Theme.surfaceAlt
        fadeDuration: Motion.settingsFade
        fadeEasing: Motion.ease
        rippleEnabled: true
        focusShown: control.visualFocus
    }

    T.Popup {
        id: flyout

        readonly property bool anchoredUnder: parent !== null && parent.width >= control.flyoutMinViewport

        parent: T.Overlay.overlay
        // width: min(calc(100% - 32px), 320px)
        width: Math.min(320, (parent ? parent.width : 320) - 32)
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: 8
        modal: true
        focus: true
        closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

        onAboutToShow: {
            App.regionLanguages.regionSearch = ""
            place()
        }
        onOpened: {
            if (search.enabled)
                search.forceActiveFocus(Qt.PopupFocusReason)
        }
        onImplicitHeightChanged: if (visible) place()

        // Under the button, its right edge on the button's, kept 12px inside
        // the window; above it when it would not fit below. A narrow window
        // centres it as a dialog.
        function place() {
            const overlay = parent
            if (!overlay)
                return
            if (!anchoredUnder) {
                x = Math.round((overlay.width - width) / 2)
                y = Math.round((overlay.height - implicitHeight) / 2)
                return
            }
            const box = control.mapToItem(overlay, 0, 0, control.width, control.height)
            x = Math.round(Math.max(control.flyoutEdge,
                                    Math.min(box.x + box.width - width, overlay.width - width - control.flyoutEdge)))
            const below = box.y + box.height + control.flyoutGap
            y = Math.round(below + implicitHeight <= overlay.height - control.flyoutEdge
                           ? below
                           : Math.max(control.flyoutEdge, box.y - control.flyoutGap - implicitHeight))
        }

        // Anchored, the backdrop is clear; centred it dims and blurs.
        T.Overlay.modal: Rectangle {
            color: flyout.anchoredUnder ? "transparent" : Qt.rgba(0, 0, 0, 0.45)

            BackdropBlur {
                z: -1
                visible: !flyout.anchoredUnder
                source: App.effects.windowBackdrop
                blurPx: 4
            }
        }

        enter: Transition {
            enabled: !Motion.reduced
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.settingsDialogIn; easing: Motion.ease }
            NumberAnimation { target: rise; property: "y"; from: 6; to: 0; duration: Motion.settingsDialogIn; easing: Motion.ease }
        }

        background: Item {
            transform: Translate { id: rise }

            BoxShadow {
                anchors.fill: parent
                layers: Theme.shadowFlyout
                radius: Theme.radiusMd
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: Theme.surfaceElev
                border.width: 1
                border.color: Theme.strokeStrong
            }
        }

        contentItem: Column {
            transform: Translate { y: rise.y }
            spacing: 0

            // .settings-region-search: 34px, a 1px stroke with a 2px accent
            // line at the bottom, the browse icon, then the field.
            Rectangle {
                width: parent.width
                height: 34
                radius: Theme.radiusSm
                color: Theme.surfaceAlt
                border.width: 1
                border.color: Theme.stroke
                clip: true

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    color: Theme.accentColor
                }

                Icon {
                    id: searchIcon

                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    name: "browse"
                    size: 14
                    color: Theme.fgTertiary
                }

                T.TextField {
                    id: search

                    anchors.left: searchIcon.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 2
                    enabled: App.regionLanguages.hasRegions
                    padding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    font: Theme.body
                    color: Theme.fg
                    selectionColor: Theme.accentSoft2
                    selectedTextColor: Theme.fg
                    placeholderText: qsTr("Search countries")
                    placeholderTextColor: Theme.fgTertiary
                    text: App.regionLanguages.regionSearch
                    Accessible.name: qsTr("Search countries")
                    onTextEdited: App.regionLanguages.regionSearch = text
                    // A search box spends its first Escape clearing itself; in
                    // the flyout, Escape closes.
                    Keys.onEscapePressed: flyout.close()
                }
            }

            // .settings-dialog-state: loading, or the error with Retry.
            Column {
                width: parent.width
                visible: !App.regionLanguages.hasRegions
                topPadding: 26
                bottomPadding: 26
                spacing: 12

                SettingsSpinner {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: App.regionLanguages.regionsLoading
                }

                Text {
                    width: parent.width - 16
                    x: 8
                    text: App.regionLanguages.regionsLoading ? qsTr("Loading music regions…")
                                                             : App.regionLanguages.regionError
                    font: Theme.settingsDialogState
                    color: App.regionLanguages.regionsLoading ? Theme.fgTertiary : Theme.danger
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                SettingsButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !App.regionLanguages.regionsLoading
                    text: qsTr("Retry")
                    onClicked: App.regionLanguages.retryRegions()
                }
            }

            // .settings-region-list: 6px under the search, at most
            // min(340px, 55vh) high.
            ListView {
                id: list

                width: parent.width
                visible: App.regionLanguages.hasRegions
                topMargin: 6
                height: visible ? Math.min(contentHeight + topMargin,
                                           Math.min(340, (flyout.parent ? flyout.parent.height : 0) * 0.55))
                                : 0
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: App.regionLanguages.regionChoices
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Music regions")

                T.ScrollBar.vertical: AppScrollBar {}

                delegate: T.ItemDelegate {
                    id: option

                    required property var modelData

                    width: ListView.view.width
                    implicitHeight: Math.max(36, implicitContentHeight + topPadding + bottomPadding)
                    topPadding: 6
                    bottomPadding: 6
                    leftPadding: 12
                    rightPadding: 12
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    text: modelData.name
                    font: modelData.selected ? Theme.settingsRowTitle : Theme.body
                    Accessible.role: Accessible.ListItem
                    Accessible.selected: modelData.selected

                    onClicked: {
                        flyout.close()
                        App.regionLanguages.chooseRegion(modelData.code)
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    contentItem: Text {
                        text: option.text
                        font: option.font
                        color: Theme.fg
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: Rectangle {
                        radius: 4
                        color: option.modelData.selected ? Theme.rowHover
                                                         : option.hovered ? Theme.hover : "transparent"

                        // The selected row's 3px by 16px accent mark.
                        Rectangle {
                            visible: option.modelData.selected
                            width: 3
                            height: 16
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.accentColor
                        }

                        FocusRing {
                            shown: option.visualFocus
                            controlRadius: 4
                        }
                    }
                }
            }

            Text {
                width: parent.width
                visible: App.regionLanguages.hasRegions && list.count === 0
                topPadding: 26
                bottomPadding: 26
                text: qsTr("No matching regions.")
                font: Theme.settingsDialogState
                color: Theme.fgTertiary
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
