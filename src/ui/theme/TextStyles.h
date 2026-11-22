#pragma once

#include <QFont>
#include <QString>

namespace lmp::ui {

// The three font families of the design, resolved against the installed fonts.
struct FontFamilies {
    QString text;
    QString display;
    QString expressive;
    // The glyph font of the window caption buttons.
    QString captionIcons;

    // Picks the first installed family of each design stack.
    [[nodiscard]] static FontFamilies resolve();
};

// A text style: the font and its line height in pixels. QFont has no line
// height, so text items apply it with `lineHeightMode: Text.FixedHeight`. A
// line height of 0 means the style sets none and the font's own is used.
struct TextStyle {
    QFont font;
    qreal lineHeight = 0;
};

// Every named text style of the design.
struct TextStyles {
    TextStyle body;
    TextStyle sectionTitle;
    TextStyle sidebarSectionLabel;
    TextStyle profileName;
    TextStyle playerBarTitle;
    TextStyle playerBarArtist;
    TextStyle trackTitle;
    TextStyle trackArtist;
    TextStyle modalTitle;
    TextStyle settingsHeading;
    TextStyle nowPlayingTitle;
    TextStyle fullScreenTitle;
    TextStyle homeGreeting;
    TextStyle buttonLabel;
    TextStyle settingsButtonLabel;
    TextStyle chipLabel;
    TextStyle bubbleLabel;
    TextStyle selectLabel;
    TextStyle optionLabel;
    TextStyle searchInput;
    TextStyle segmentLabel;
    TextStyle segmentSelectedLabel;
    TextStyle segmentCompactLabel;
    TextStyle shortcutLabel;
    TextStyle menuLabel;
    TextStyle modalDescription;
    TextStyle fieldLabel;
    TextStyle fieldInput;
    TextStyle errorText;
    TextStyle toastLabel;
    TextStyle toastAction;
    TextStyle artTileLabel;
    TextStyle artTileLabelBig;
    TextStyle captionGlyph;

    [[nodiscard]] static TextStyles build(const FontFamilies& families);
};

} // namespace lmp::ui
