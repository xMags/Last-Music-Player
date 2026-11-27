import QtQuick
import LastMusicPlayer

// A part of a Settings card that is not a plain row (the equalizer's bands,
// a scan's progress, the language panel): its content is its child, padded
// 14px by 20px under the card's 1px line. `shown` is its own condition;
// while the page is searched it shows when its card matches or its
// `keywords` do.
Item {
    id: block

    property bool shown: true
    property string keywords: ""
    // Continues the row above: no line between them, no padding above.
    property bool joined: false
    // Padding above and below the content (the card's line comes first).
    property int topPadding: joined ? 0 : 14
    property int bottomPadding: 14
    default property alias content: body.data

    width: parent ? parent.width : 0
    visible: shown && search.matches
    implicitHeight: (joined ? 0 : 1) + topPadding + body.childrenRect.height + bottomPadding

    SettingsSearchable {
        id: search

        row: block
        searchText: block.keywords
    }

    Item {
        id: body

        x: 20
        y: (block.joined ? 0 : 1) + block.topPadding
        width: parent.width - 2 * 20
        height: childrenRect.height
    }
}
