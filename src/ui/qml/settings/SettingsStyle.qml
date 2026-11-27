pragma Singleton

import QtQuick
import LastMusicPlayer

// The Settings page's look and its search. The colours are the redesign's
// own in the dark theme (#2B2B2B cards, #353535 lines, #A6A6A6 descriptions,
// #333333 buttons with a #4A4A4A border...), mixed from the theme's colours,
// so the light theme follows with the same contrast. `query` is what the
// search box holds; a row, card or group shows while it matches
// (`matches`), or while the card or group around it does.
QtObject {
    // A card, and the 1px line above each of its rows but the first.
    readonly property color card: Theme.surface
    readonly property color line: Theme.mixSrgb(Theme.fg, Theme.surface, 0.047)
    // A card's uppercase label, a row's title and its description.
    readonly property color label: Theme.fgTertiary
    readonly property color title: Theme.fg
    readonly property color description: Theme.mixSrgb(Theme.fgSecondary, Theme.fgTertiary, 0.26)
    // Buttons and dropdowns: the face, its hover and the border.
    readonly property color control: Theme.mixSrgb(Theme.fg, Theme.surface, 0.038)
    readonly property color controlHover: Theme.mixSrgb(Theme.fg, Theme.surface, 0.08)
    readonly property color controlBorder: Theme.mixSrgb(Theme.fg, Theme.surface, 0.146)
    // Text fields, the off switch and the segmented track sit on the page's
    // own colour.
    readonly property color field: Theme.canvas
    // Source cards' and theme tiles' borders, and the chosen segment.
    readonly property color quiet: Theme.mixSrgb(Theme.fg, Theme.surface, 0.07)
    readonly property color quietHover: Theme.mixSrgb(Theme.fg, Theme.surface, 0.22)
    // The off switch's border and a status dot that is not ready.
    readonly property color off: Theme.mixSrgb(Theme.fg, Theme.surface, 0.316)
    readonly property color chipText: Theme.mixSrgb(Theme.fg, Theme.fgSecondary, 0.6)
    readonly property color tile: Theme.mixSrgb(Theme.canvas, Theme.surface, 0.55)
    // The source card in use.
    readonly property color chosenFill: Theme.mixSrgb(Theme.accentColor, Theme.surface, 0.06)
    // The card of what cannot be undone, and its button.
    readonly property color dangerText: Theme.mixSrgb(Theme.danger, Theme.fg, 0.85)
    readonly property color dangerFill: Theme.mixSrgb(Theme.danger, Theme.canvas, 0.05)
    readonly property color dangerBorder: Theme.mixSrgb(Theme.danger, Theme.surface, 0.25)
    readonly property color dangerLine: Theme.mixSrgb(Theme.danger, Theme.surface, 0.12)
    readonly property color dangerButton: Theme.mixSrgb(Theme.danger, Theme.surface, 0.04)
    readonly property color dangerButtonBorder: Theme.mixSrgb(Theme.danger, Theme.surface, 0.35)

    readonly property string family: Theme.fontText

    // The search box's text; the page clears it when it closes.
    property string query: ""
    readonly property string needle: query.trim().toLowerCase()

    function matches(text: string): bool {
        return needle === "" || text.toLowerCase().indexOf(needle) >= 0
    }
}
