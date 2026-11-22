#pragma once

#include <QFont>
#include <QObject>
#include <QString>
#include <QVariantList>

#include "ui/theme/TextStyles.h"

namespace lmp::ui {

// The part of the theme that is the same in light and dark and for every
// accent: corner radii, fixed layout sizes, fonts, text styles and the fixed
// colour lists. Theme derives from it so QML reads every token flat, as
// `Theme.radiusMd`.
class ThemeConstants : public QObject {
    Q_OBJECT

    // Corner scale.
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT FINAL)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT FINAL)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT FINAL)
    Q_PROPERTY(int radiusXl READ radiusXl CONSTANT FINAL)
    Q_PROPERTY(int radius2xl READ radius2xl CONSTANT FINAL)

    // Layout sizes. The wide values apply from wideWindowMinWidth upwards.
    Q_PROPERTY(int playerBarHeight READ playerBarHeight CONSTANT FINAL)
    Q_PROPERTY(int sidebarLeftWidth READ sidebarLeftWidth CONSTANT FINAL)
    Q_PROPERTY(int sidebarRightWidth READ sidebarRightWidth CONSTANT FINAL)
    Q_PROPERTY(int sidebarLeftWidthWide READ sidebarLeftWidthWide CONSTANT FINAL)
    Q_PROPERTY(int sidebarRightWidthWide READ sidebarRightWidthWide CONSTANT FINAL)
    Q_PROPERTY(int wideWindowMinWidth READ wideWindowMinWidth CONSTANT FINAL)
    Q_PROPERTY(int titleBarHeight READ titleBarHeight CONSTANT FINAL)

    // Each entry is a list of two colours: gradient start and end.
    Q_PROPERTY(QVariantList artworkGradients READ artworkGradientList CONSTANT FINAL)
    // The five accent seed colours, in Theme.Accent order, for the accent picker.
    Q_PROPERTY(QVariantList accentSeeds READ accentSeedList CONSTANT FINAL)

    // Font families of the design stacks, resolved against installed fonts.
    Q_PROPERTY(QString fontText READ fontText CONSTANT FINAL)
    Q_PROPERTY(QString fontDisplay READ fontDisplay CONSTANT FINAL)
    Q_PROPERTY(QString fontExpressive READ fontExpressive CONSTANT FINAL)

    // Text styles. Each line height is in pixels, for Text.FixedHeight.
    Q_PROPERTY(QFont body READ body CONSTANT FINAL)
    Q_PROPERTY(QFont sectionTitle READ sectionTitle CONSTANT FINAL)
    Q_PROPERTY(QFont sidebarSectionLabel READ sidebarSectionLabel CONSTANT FINAL)
    Q_PROPERTY(QFont profileName READ profileName CONSTANT FINAL)
    Q_PROPERTY(QFont playerBarTitle READ playerBarTitle CONSTANT FINAL)
    Q_PROPERTY(QFont playerBarArtist READ playerBarArtist CONSTANT FINAL)
    Q_PROPERTY(QFont trackTitle READ trackTitle CONSTANT FINAL)
    Q_PROPERTY(QFont trackArtist READ trackArtist CONSTANT FINAL)
    Q_PROPERTY(QFont modalTitle READ modalTitle CONSTANT FINAL)
    Q_PROPERTY(QFont settingsHeading READ settingsHeading CONSTANT FINAL)
    Q_PROPERTY(QFont nowPlayingTitle READ nowPlayingTitle CONSTANT FINAL)
    Q_PROPERTY(QFont fullScreenTitle READ fullScreenTitle CONSTANT FINAL)
    Q_PROPERTY(QFont homeGreeting READ homeGreeting CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSectionTitle READ lineHeightSectionTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSidebarSectionLabel READ lineHeightSidebarSectionLabel CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightPlayerBarTitle READ lineHeightPlayerBarTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightPlayerBarArtist READ lineHeightPlayerBarArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightTrackTitle READ lineHeightTrackTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightTrackArtist READ lineHeightTrackArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightModalTitle READ lineHeightModalTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSettingsHeading READ lineHeightSettingsHeading CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightNowPlayingTitle READ lineHeightNowPlayingTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightFullScreenTitle READ lineHeightFullScreenTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightHomeGreeting READ lineHeightHomeGreeting CONSTANT FINAL)

public:
    explicit ThemeConstants(QObject* parent = nullptr);

    [[nodiscard]] int radiusSm() const { return 6; }
    [[nodiscard]] int radiusMd() const { return 8; }
    [[nodiscard]] int radiusLg() const { return 12; }
    [[nodiscard]] int radiusXl() const { return 14; }
    [[nodiscard]] int radius2xl() const { return 16; }

    [[nodiscard]] int playerBarHeight() const { return 90; }
    [[nodiscard]] int sidebarLeftWidth() const { return 240; }
    [[nodiscard]] int sidebarRightWidth() const { return 300; }
    [[nodiscard]] int sidebarLeftWidthWide() const { return 300; }
    [[nodiscard]] int sidebarRightWidthWide() const { return 380; }
    [[nodiscard]] int wideWindowMinWidth() const { return 1500; }
    [[nodiscard]] int titleBarHeight() const { return 32; }

    [[nodiscard]] static QVariantList artworkGradientList();
    [[nodiscard]] static QVariantList accentSeedList();

    [[nodiscard]] QString fontText() const { return m_families.text; }
    [[nodiscard]] QString fontDisplay() const { return m_families.display; }
    [[nodiscard]] QString fontExpressive() const { return m_families.expressive; }

    [[nodiscard]] QFont body() const { return m_text.body.font; }
    [[nodiscard]] QFont sectionTitle() const { return m_text.sectionTitle.font; }
    [[nodiscard]] QFont sidebarSectionLabel() const { return m_text.sidebarSectionLabel.font; }
    [[nodiscard]] QFont profileName() const { return m_text.profileName.font; }
    [[nodiscard]] QFont playerBarTitle() const { return m_text.playerBarTitle.font; }
    [[nodiscard]] QFont playerBarArtist() const { return m_text.playerBarArtist.font; }
    [[nodiscard]] QFont trackTitle() const { return m_text.trackTitle.font; }
    [[nodiscard]] QFont trackArtist() const { return m_text.trackArtist.font; }
    [[nodiscard]] QFont modalTitle() const { return m_text.modalTitle.font; }
    [[nodiscard]] QFont settingsHeading() const { return m_text.settingsHeading.font; }
    [[nodiscard]] QFont nowPlayingTitle() const { return m_text.nowPlayingTitle.font; }
    [[nodiscard]] QFont fullScreenTitle() const { return m_text.fullScreenTitle.font; }
    [[nodiscard]] QFont homeGreeting() const { return m_text.homeGreeting.font; }
    [[nodiscard]] qreal lineHeightSectionTitle() const { return m_text.sectionTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSidebarSectionLabel() const { return m_text.sidebarSectionLabel.lineHeight; }
    [[nodiscard]] qreal lineHeightPlayerBarTitle() const { return m_text.playerBarTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightPlayerBarArtist() const { return m_text.playerBarArtist.lineHeight; }
    [[nodiscard]] qreal lineHeightTrackTitle() const { return m_text.trackTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightTrackArtist() const { return m_text.trackArtist.lineHeight; }
    [[nodiscard]] qreal lineHeightModalTitle() const { return m_text.modalTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSettingsHeading() const { return m_text.settingsHeading.lineHeight; }
    [[nodiscard]] qreal lineHeightNowPlayingTitle() const { return m_text.nowPlayingTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightFullScreenTitle() const { return m_text.fullScreenTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightHomeGreeting() const { return m_text.homeGreeting.lineHeight; }

private:
    FontFamilies m_families;
    TextStyles m_text;
};

} // namespace lmp::ui
