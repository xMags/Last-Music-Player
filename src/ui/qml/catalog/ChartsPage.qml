pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Every chart of a Home row (catalog.ts renderChartList): Back, the CHARTS
// eyebrow and the row's title, then the charts as a grid of cards, as many
// columns as fit at 160px or more, 20px apart (.chart-card-grid). The
// charts come from Home's discovery (App.home).
Column {
    id: page

    // "explore" (genre charts) or "cities" (city charts).
    readonly property string section: App.navigator.current.param("section")
    readonly property string rowKey: section === "cities" ? "cityCharts" : "genreCharts"
    readonly property ChartCardModel charts: App.home.chartRow(page.rowKey)
    readonly property real gutter: Theme.contentGutter
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane
    readonly property real inner: width - gutter - rightGutter
    readonly property int columns: Math.max(1, Math.floor((inner + 20) / (160 + 20)))
    readonly property real cardSize: (inner - (columns - 1) * 20) / columns

    leftPadding: gutter
    rightPadding: rightGutter
    // .catalog-page: padding 2px 0 40px.
    topPadding: 2
    bottomPadding: 40

    // .catalog-back: margin 16px 0 22px, 12.5px 650 in the accent.
    Item { width: 1; height: 16 }

    Item {
        implicitWidth: backRow.implicitWidth
        implicitHeight: backRow.implicitHeight

        Row {
            id: backRow

            spacing: 6

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron"
                size: 14
                rotation: 180
                color: Theme.accentColor
            }

            Text {
                text: qsTr("Back")
                font: Theme.collectionBack
                color: Theme.accentColor
            }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: App.navigator.back() }
        Accessible.role: Accessible.Link
        Accessible.name: qsTr("Back")
    }

    Item { width: 1; height: 22 }

    // .view-header: padding 18px 0 22px.
    Column {
        width: page.inner
        topPadding: 18
        bottomPadding: 22

        Text {
            bottomPadding: 4
            text: qsTr("CHARTS")
            font: Theme.eyebrow
            color: Theme.fgSecondary
        }

        Text {
            width: parent.width
            text: App.home.chartsTitle(page.rowKey)
            font: Theme.viewTitle
            color: Theme.fg
            elide: Text.ElideRight
        }
    }

    CompactEmptyState {
        width: page.inner
        visible: page.charts.count === 0
        compact: false
        text: qsTr("These charts are unavailable.")
    }

    // margin-top: 8px.
    Item { width: 1; height: 8 }

    Flow {
        width: page.inner
        spacing: 20

        Repeater {
            model: page.charts

            ChartCard {
                id: chartCard

                required property int index
                required artSource
                required artworkUrl
                required name
                required family
                required kindLabel
                required kindColor
                required cardSubtitle

                width: page.cardSize
                height: page.cardSize
                onClicked: App.home.openChart(page.rowKey, chartCard.index)
            }
        }
    }
}
