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
    // The sign-in dialog's two bundled faces: an expanded black display face
    // for its title and a monospace face for its small labels.
    QString sleeveDisplay;
    QString sleeveMono;
    // A monospace face for small labels (the genre tile's).
    QString mono;

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

// What a text style is made from, in CSS terms.
struct TextStyleSpec {
    const QString& family;
    qreal pixelSize;
    // The CSS 100 to 1000 scale, which QFont shares.
    int weight;
    qreal lineHeight = 0;
    // CSS letter-spacing in em; converted to pixels for QFont.
    qreal letterSpacingEm = 0;
    bool uppercase = false;
    // font-variant-numeric: tabular-nums, so changing digits do not shift.
    bool tabularNumbers = false;
};

// Windows' text size (Settings > Accessibility > Text size) as a factor,
// 1 to 2.25. Every text style is made at this size.
[[nodiscard]] qreal systemTextScale();

[[nodiscard]] TextStyle makeTextStyle(const TextStyleSpec& spec);

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
    TextStyle settingsSection;
    TextStyle settingsRowTitle;
    TextStyle settingsRowDescription;
    TextStyle settingsTileLabel;
    TextStyle settingsHeroName;
    TextStyle settingsAvatarLetter;
    TextStyle settingsTier;
    TextStyle settingsDialogText;
    TextStyle settingsComboLabel;
    TextStyle settingsDialogState;
    TextStyle settingsFeedback;
    TextStyle signInTitle;
    TextStyle signInLabel;
    TextStyle signInTagline;
    TextStyle signInButton;
    TextStyle signInError;
    TextStyle trackNumber;
    TextStyle trackAlbum;
    TextStyle cardTitle;
    TextStyle cardSubtitle;
    TextStyle sectionLink;
    TextStyle stateTitle;
    TextStyle stateText;
    TextStyle tableHeading;
    TextStyle eyebrow;
    TextStyle pill;
    TextStyle genreLabel;
    TextStyle collectionBack;
    TextStyle collectionEyebrow;
    TextStyle collectionTitle;
    TextStyle collectionInfo;
    TextStyle collectionInfoStrong;
    TextStyle collectionDescription;
    TextStyle collectionPlay;
    TextStyle homeLabel;
    TextStyle heroTitle;
    TextStyle heroSub;
    TextStyle quickTitle;
    TextStyle mixBadge;
    TextStyle mixBlurb;
    TextStyle searchCategory;
    TextStyle searchTopTitle;
    TextStyle searchTopKind;
    TextStyle nowPlayingTitle;
    TextStyle fullScreenTitle;
    TextStyle homeGreeting;
    TextStyle buttonLabel;
    TextStyle settingsButtonLabel;
    TextStyle settingsButtonSmallLabel;
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
    TextStyle brandLabel;
    TextStyle navLabel;
    TextStyle navLabelActive;
    TextStyle profileTier;
    TextStyle profileDots;
    TextStyle playerTime;
    TextStyle repeatBadge;
    TextStyle nowPlayingArtist;
    TextStyle queueDuration;
    TextStyle queueAction;
    TextStyle queueStatus;
    TextStyle emptyStateCompact;

    [[nodiscard]] static TextStyles build(const FontFamilies& families);
};

} // namespace lmp::ui
