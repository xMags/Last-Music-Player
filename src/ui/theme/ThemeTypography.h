#pragma once

#include <QFont>
#include <QObject>
#include <QString>

#include "ui/theme/TextStyles.h"

namespace lmp::ui {

// Font families and named text styles, resolved once against the installed
// fonts. Part of the Theme singleton; each line height is in pixels, for
// `lineHeightMode: Text.FixedHeight`.
class ThemeTypography : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString fontText READ fontText CONSTANT FINAL)
    Q_PROPERTY(QString fontDisplay READ fontDisplay CONSTANT FINAL)
    Q_PROPERTY(QString fontExpressive READ fontExpressive CONSTANT FINAL)
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
    Q_PROPERTY(QFont settingsSection READ settingsSection CONSTANT FINAL)
    Q_PROPERTY(QFont settingsRowTitle READ settingsRowTitle CONSTANT FINAL)
    Q_PROPERTY(QFont settingsRowDescription READ settingsRowDescription CONSTANT FINAL)
    Q_PROPERTY(QFont settingsTileLabel READ settingsTileLabel CONSTANT FINAL)
    Q_PROPERTY(QFont signInTitle READ signInTitle CONSTANT FINAL)
    Q_PROPERTY(QFont signInLabel READ signInLabel CONSTANT FINAL)
    Q_PROPERTY(QFont signInTagline READ signInTagline CONSTANT FINAL)
    Q_PROPERTY(QFont signInButton READ signInButton CONSTANT FINAL)
    Q_PROPERTY(QFont signInError READ signInError CONSTANT FINAL)
    Q_PROPERTY(QFont trackNumber READ trackNumber CONSTANT FINAL)
    Q_PROPERTY(QFont trackAlbum READ trackAlbum CONSTANT FINAL)
    Q_PROPERTY(QFont cardTitle READ cardTitle CONSTANT FINAL)
    Q_PROPERTY(QFont cardSubtitle READ cardSubtitle CONSTANT FINAL)
    Q_PROPERTY(QFont sectionLink READ sectionLink CONSTANT FINAL)
    Q_PROPERTY(QFont stateTitle READ stateTitle CONSTANT FINAL)
    Q_PROPERTY(QFont stateText READ stateText CONSTANT FINAL)
    Q_PROPERTY(QFont tableHeading READ tableHeading CONSTANT FINAL)
    Q_PROPERTY(QFont eyebrow READ eyebrow CONSTANT FINAL)
    Q_PROPERTY(QFont pill READ pill CONSTANT FINAL)
    Q_PROPERTY(QFont genreLabel READ genreLabel CONSTANT FINAL)
    Q_PROPERTY(QFont collectionBack READ collectionBack CONSTANT FINAL)
    Q_PROPERTY(QFont collectionEyebrow READ collectionEyebrow CONSTANT FINAL)
    Q_PROPERTY(QFont collectionTitle READ collectionTitle CONSTANT FINAL)
    Q_PROPERTY(QFont collectionInfo READ collectionInfo CONSTANT FINAL)
    Q_PROPERTY(QFont collectionInfoStrong READ collectionInfoStrong CONSTANT FINAL)
    Q_PROPERTY(QFont collectionDescription READ collectionDescription CONSTANT FINAL)
    Q_PROPERTY(QFont collectionPlay READ collectionPlay CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightCollectionTitle READ lineHeightCollectionTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightCollectionDescription READ lineHeightCollectionDescription CONSTANT FINAL)
    Q_PROPERTY(QFont nowPlayingTitle READ nowPlayingTitle CONSTANT FINAL)
    Q_PROPERTY(QFont fullScreenTitle READ fullScreenTitle CONSTANT FINAL)
    Q_PROPERTY(QFont homeGreeting READ homeGreeting CONSTANT FINAL)
    Q_PROPERTY(QFont buttonLabel READ buttonLabel CONSTANT FINAL)
    Q_PROPERTY(QFont settingsButtonLabel READ settingsButtonLabel CONSTANT FINAL)
    Q_PROPERTY(QFont chipLabel READ chipLabel CONSTANT FINAL)
    Q_PROPERTY(QFont bubbleLabel READ bubbleLabel CONSTANT FINAL)
    Q_PROPERTY(QFont selectLabel READ selectLabel CONSTANT FINAL)
    Q_PROPERTY(QFont optionLabel READ optionLabel CONSTANT FINAL)
    Q_PROPERTY(QFont searchInput READ searchInput CONSTANT FINAL)
    Q_PROPERTY(QFont segmentLabel READ segmentLabel CONSTANT FINAL)
    Q_PROPERTY(QFont segmentSelectedLabel READ segmentSelectedLabel CONSTANT FINAL)
    Q_PROPERTY(QFont segmentCompactLabel READ segmentCompactLabel CONSTANT FINAL)
    Q_PROPERTY(QFont shortcutLabel READ shortcutLabel CONSTANT FINAL)
    Q_PROPERTY(QFont menuLabel READ menuLabel CONSTANT FINAL)
    Q_PROPERTY(QFont modalDescription READ modalDescription CONSTANT FINAL)
    Q_PROPERTY(QFont fieldLabel READ fieldLabel CONSTANT FINAL)
    Q_PROPERTY(QFont fieldInput READ fieldInput CONSTANT FINAL)
    Q_PROPERTY(QFont errorText READ errorText CONSTANT FINAL)
    Q_PROPERTY(QFont toastLabel READ toastLabel CONSTANT FINAL)
    Q_PROPERTY(QFont toastAction READ toastAction CONSTANT FINAL)
    Q_PROPERTY(QFont artTileLabel READ artTileLabel CONSTANT FINAL)
    Q_PROPERTY(QFont artTileLabelBig READ artTileLabelBig CONSTANT FINAL)
    Q_PROPERTY(QFont captionGlyph READ captionGlyph CONSTANT FINAL)
    Q_PROPERTY(QFont brandLabel READ brandLabel CONSTANT FINAL)
    Q_PROPERTY(QFont navLabel READ navLabel CONSTANT FINAL)
    Q_PROPERTY(QFont navLabelActive READ navLabelActive CONSTANT FINAL)
    Q_PROPERTY(QFont profileTier READ profileTier CONSTANT FINAL)
    Q_PROPERTY(QFont profileDots READ profileDots CONSTANT FINAL)
    Q_PROPERTY(QFont playerTime READ playerTime CONSTANT FINAL)
    Q_PROPERTY(QFont repeatBadge READ repeatBadge CONSTANT FINAL)
    Q_PROPERTY(QFont nowPlayingArtist READ nowPlayingArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightNowPlayingArtist READ lineHeightNowPlayingArtist CONSTANT FINAL)
    Q_PROPERTY(QFont queueDuration READ queueDuration CONSTANT FINAL)
    Q_PROPERTY(QFont queueAction READ queueAction CONSTANT FINAL)
    Q_PROPERTY(QFont queueStatus READ queueStatus CONSTANT FINAL)
    Q_PROPERTY(QFont emptyStateCompact READ emptyStateCompact CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightModalDescription READ lineHeightModalDescription CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightBubbleLabel READ lineHeightBubbleLabel CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSectionTitle READ lineHeightSectionTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSidebarSectionLabel READ lineHeightSidebarSectionLabel CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightPlayerBarTitle READ lineHeightPlayerBarTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightPlayerBarArtist READ lineHeightPlayerBarArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightTrackTitle READ lineHeightTrackTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightTrackArtist READ lineHeightTrackArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightModalTitle READ lineHeightModalTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSettingsHeading READ lineHeightSettingsHeading CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSettingsSection READ lineHeightSettingsSection CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSettingsRowTitle READ lineHeightSettingsRowTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSettingsRowDescription READ lineHeightSettingsRowDescription CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSignInTitle READ lineHeightSignInTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSignInTagline READ lineHeightSignInTagline CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightSignInError READ lineHeightSignInError CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightCardTitle READ lineHeightCardTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightCardSubtitle READ lineHeightCardSubtitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightStateText READ lineHeightStateText CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightNowPlayingTitle READ lineHeightNowPlayingTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightFullScreenTitle READ lineHeightFullScreenTitle CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightHomeGreeting READ lineHeightHomeGreeting CONSTANT FINAL)

public:
    explicit ThemeTypography(QObject* parent = nullptr);

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
    [[nodiscard]] QFont settingsSection() const { return m_text.settingsSection.font; }
    [[nodiscard]] QFont settingsRowTitle() const { return m_text.settingsRowTitle.font; }
    [[nodiscard]] QFont settingsRowDescription() const { return m_text.settingsRowDescription.font; }
    [[nodiscard]] QFont settingsTileLabel() const { return m_text.settingsTileLabel.font; }
    [[nodiscard]] QFont signInTitle() const { return m_text.signInTitle.font; }
    [[nodiscard]] QFont signInLabel() const { return m_text.signInLabel.font; }
    [[nodiscard]] QFont signInTagline() const { return m_text.signInTagline.font; }
    [[nodiscard]] QFont signInButton() const { return m_text.signInButton.font; }
    [[nodiscard]] QFont signInError() const { return m_text.signInError.font; }
    [[nodiscard]] QFont trackNumber() const { return m_text.trackNumber.font; }
    [[nodiscard]] QFont trackAlbum() const { return m_text.trackAlbum.font; }
    [[nodiscard]] QFont cardTitle() const { return m_text.cardTitle.font; }
    [[nodiscard]] QFont cardSubtitle() const { return m_text.cardSubtitle.font; }
    [[nodiscard]] QFont sectionLink() const { return m_text.sectionLink.font; }
    [[nodiscard]] QFont stateTitle() const { return m_text.stateTitle.font; }
    [[nodiscard]] QFont stateText() const { return m_text.stateText.font; }
    [[nodiscard]] QFont tableHeading() const { return m_text.tableHeading.font; }
    [[nodiscard]] QFont eyebrow() const { return m_text.eyebrow.font; }
    [[nodiscard]] QFont pill() const { return m_text.pill.font; }
    [[nodiscard]] QFont genreLabel() const { return m_text.genreLabel.font; }
    [[nodiscard]] QFont collectionBack() const { return m_text.collectionBack.font; }
    [[nodiscard]] QFont collectionEyebrow() const { return m_text.collectionEyebrow.font; }
    [[nodiscard]] QFont collectionTitle() const { return m_text.collectionTitle.font; }
    [[nodiscard]] QFont collectionInfo() const { return m_text.collectionInfo.font; }
    [[nodiscard]] QFont collectionInfoStrong() const { return m_text.collectionInfoStrong.font; }
    [[nodiscard]] QFont collectionDescription() const { return m_text.collectionDescription.font; }
    [[nodiscard]] QFont collectionPlay() const { return m_text.collectionPlay.font; }
    [[nodiscard]] qreal lineHeightCollectionTitle() const { return m_text.collectionTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightCollectionDescription() const { return m_text.collectionDescription.lineHeight; }
    [[nodiscard]] QFont nowPlayingTitle() const { return m_text.nowPlayingTitle.font; }
    [[nodiscard]] QFont fullScreenTitle() const { return m_text.fullScreenTitle.font; }
    [[nodiscard]] QFont homeGreeting() const { return m_text.homeGreeting.font; }
    [[nodiscard]] QFont buttonLabel() const { return m_text.buttonLabel.font; }
    [[nodiscard]] QFont settingsButtonLabel() const { return m_text.settingsButtonLabel.font; }
    [[nodiscard]] QFont chipLabel() const { return m_text.chipLabel.font; }
    [[nodiscard]] QFont bubbleLabel() const { return m_text.bubbleLabel.font; }
    [[nodiscard]] QFont selectLabel() const { return m_text.selectLabel.font; }
    [[nodiscard]] QFont optionLabel() const { return m_text.optionLabel.font; }
    [[nodiscard]] QFont searchInput() const { return m_text.searchInput.font; }
    [[nodiscard]] QFont segmentLabel() const { return m_text.segmentLabel.font; }
    [[nodiscard]] QFont segmentSelectedLabel() const { return m_text.segmentSelectedLabel.font; }
    [[nodiscard]] QFont segmentCompactLabel() const { return m_text.segmentCompactLabel.font; }
    [[nodiscard]] QFont shortcutLabel() const { return m_text.shortcutLabel.font; }
    [[nodiscard]] QFont menuLabel() const { return m_text.menuLabel.font; }
    [[nodiscard]] QFont modalDescription() const { return m_text.modalDescription.font; }
    [[nodiscard]] QFont fieldLabel() const { return m_text.fieldLabel.font; }
    [[nodiscard]] QFont fieldInput() const { return m_text.fieldInput.font; }
    [[nodiscard]] QFont errorText() const { return m_text.errorText.font; }
    [[nodiscard]] QFont toastLabel() const { return m_text.toastLabel.font; }
    [[nodiscard]] QFont toastAction() const { return m_text.toastAction.font; }
    [[nodiscard]] QFont artTileLabel() const { return m_text.artTileLabel.font; }
    [[nodiscard]] QFont artTileLabelBig() const { return m_text.artTileLabelBig.font; }
    [[nodiscard]] QFont captionGlyph() const { return m_text.captionGlyph.font; }
    [[nodiscard]] QFont brandLabel() const { return m_text.brandLabel.font; }
    [[nodiscard]] QFont navLabel() const { return m_text.navLabel.font; }
    [[nodiscard]] QFont navLabelActive() const { return m_text.navLabelActive.font; }
    [[nodiscard]] QFont profileTier() const { return m_text.profileTier.font; }
    [[nodiscard]] QFont profileDots() const { return m_text.profileDots.font; }
    [[nodiscard]] QFont playerTime() const { return m_text.playerTime.font; }
    [[nodiscard]] QFont repeatBadge() const { return m_text.repeatBadge.font; }
    [[nodiscard]] QFont nowPlayingArtist() const { return m_text.nowPlayingArtist.font; }
    [[nodiscard]] qreal lineHeightNowPlayingArtist() const { return m_text.nowPlayingArtist.lineHeight; }
    [[nodiscard]] QFont queueDuration() const { return m_text.queueDuration.font; }
    [[nodiscard]] QFont queueAction() const { return m_text.queueAction.font; }
    [[nodiscard]] QFont queueStatus() const { return m_text.queueStatus.font; }
    [[nodiscard]] QFont emptyStateCompact() const { return m_text.emptyStateCompact.font; }
    [[nodiscard]] qreal lineHeightModalDescription() const { return m_text.modalDescription.lineHeight; }
    [[nodiscard]] qreal lineHeightBubbleLabel() const { return m_text.bubbleLabel.lineHeight; }
    [[nodiscard]] qreal lineHeightSectionTitle() const { return m_text.sectionTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSidebarSectionLabel() const { return m_text.sidebarSectionLabel.lineHeight; }
    [[nodiscard]] qreal lineHeightPlayerBarTitle() const { return m_text.playerBarTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightPlayerBarArtist() const { return m_text.playerBarArtist.lineHeight; }
    [[nodiscard]] qreal lineHeightTrackTitle() const { return m_text.trackTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightTrackArtist() const { return m_text.trackArtist.lineHeight; }
    [[nodiscard]] qreal lineHeightModalTitle() const { return m_text.modalTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSettingsHeading() const { return m_text.settingsHeading.lineHeight; }
    [[nodiscard]] qreal lineHeightSettingsSection() const { return m_text.settingsSection.lineHeight; }
    [[nodiscard]] qreal lineHeightSettingsRowTitle() const { return m_text.settingsRowTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSettingsRowDescription() const { return m_text.settingsRowDescription.lineHeight; }
    [[nodiscard]] qreal lineHeightSignInTitle() const { return m_text.signInTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightSignInTagline() const { return m_text.signInTagline.lineHeight; }
    [[nodiscard]] qreal lineHeightSignInError() const { return m_text.signInError.lineHeight; }
    [[nodiscard]] qreal lineHeightCardTitle() const { return m_text.cardTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightCardSubtitle() const { return m_text.cardSubtitle.lineHeight; }
    [[nodiscard]] qreal lineHeightStateText() const { return m_text.stateText.lineHeight; }
    [[nodiscard]] qreal lineHeightNowPlayingTitle() const { return m_text.nowPlayingTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightFullScreenTitle() const { return m_text.fullScreenTitle.lineHeight; }
    [[nodiscard]] qreal lineHeightHomeGreeting() const { return m_text.homeGreeting.lineHeight; }

private:
    FontFamilies m_families;
    TextStyles m_text;
};

} // namespace lmp::ui
