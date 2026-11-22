#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QVariantList>

#include "ui/theme/TextStyles.h"
#include "ui/theme/ThemeTypography.h"

namespace lmp::ui {

// The tokens of the full-screen player, which is dark whatever the theme:
// its colours (white text at set opacities over a near-black backdrop), the
// backdrop's blur, its shadows and its text styles. Part of the Theme
// singleton, read flat as `Theme.fullscreenBackground`.
class ThemeFullscreen : public ThemeTypography {
    Q_OBJECT

    Q_PROPERTY(QColor fullscreenBackground READ fullscreenBackground CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenText READ fullscreenText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenSecondaryText READ fullscreenSecondaryText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenTertiaryText READ fullscreenTertiaryText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenArtistText READ fullscreenArtistText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenMetaText READ fullscreenMetaText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenTimeText READ fullscreenTimeText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenEmptyText READ fullscreenEmptyText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenButtonGlyph READ fullscreenButtonGlyph CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenButtonHover READ fullscreenButtonHover CONSTANT FINAL)
    // A pointed-at Up Next row in the side panel.
    Q_PROPERTY(QColor fullscreenRowHover READ fullscreenRowHover CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenPlayFill READ fullscreenPlayFill CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenPlayGlyph READ fullscreenPlayGlyph CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenSeekFill READ fullscreenSeekFill CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenSeekTrack READ fullscreenSeekTrack CONSTANT FINAL)
    // The Lyrics / Up Next switch.
    Q_PROPERTY(QColor fullscreenTabsFill READ fullscreenTabsFill CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenTabsBorder READ fullscreenTabsBorder CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenTabText READ fullscreenTabText CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenTabActiveFill READ fullscreenTabActiveFill CONSTANT FINAL)
    // The glass side panel.
    Q_PROPERTY(QColor fullscreenPanelFill READ fullscreenPanelFill CONSTANT FINAL)
    Q_PROPERTY(QColor fullscreenPanelBorder READ fullscreenPanelBorder CONSTANT FINAL)
    // The backdrop without a cover runs from the accent fill to this colour.
    Q_PROPERTY(QColor fullscreenBackdropEnd READ fullscreenBackdropEnd CONSTANT FINAL)
    // Laid over the blurred cover: black at the share that brightness(0.42) removes.
    Q_PROPERTY(QColor fullscreenBackdropDim READ fullscreenBackdropDim CONSTANT FINAL)
    // Caption buttons of the title strip over the player.
    Q_PROPERTY(QColor fullscreenCaptionGlyph READ fullscreenCaptionGlyph CONSTANT FINAL)

    // The backdrop: blur radius in pixels, saturation factor, scale, and how
    // far it reaches past each edge.
    Q_PROPERTY(int fullscreenBackdropBlur READ fullscreenBackdropBlur CONSTANT FINAL)
    Q_PROPERTY(qreal fullscreenBackdropSaturation READ fullscreenBackdropSaturation CONSTANT FINAL)
    Q_PROPERTY(qreal fullscreenBackdropScale READ fullscreenBackdropScale CONSTANT FINAL)
    Q_PROPERTY(int fullscreenBackdropInset READ fullscreenBackdropInset CONSTANT FINAL)
    Q_PROPERTY(int fullscreenArtRadius READ fullscreenArtRadius CONSTANT FINAL)
    Q_PROPERTY(int fullscreenPanelRadius READ fullscreenPanelRadius CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowFullscreenArt READ shadowFullscreenArt CONSTANT FINAL)
    Q_PROPERTY(QVariantList shadowFullscreenPlay READ shadowFullscreenPlay CONSTANT FINAL)

    Q_PROPERTY(QFont fullscreenArtist READ fullscreenArtist CONSTANT FINAL)
    Q_PROPERTY(qreal lineHeightFullscreenArtist READ lineHeightFullscreenArtist CONSTANT FINAL)
    Q_PROPERTY(QFont fullscreenMeta READ fullscreenMeta CONSTANT FINAL)
    Q_PROPERTY(QFont fullscreenTime READ fullscreenTime CONSTANT FINAL)
    Q_PROPERTY(QFont fullscreenTab READ fullscreenTab CONSTANT FINAL)
    Q_PROPERTY(QFont fullscreenTabActive READ fullscreenTabActive CONSTANT FINAL)
    Q_PROPERTY(QFont fullscreenRepeatBadge READ fullscreenRepeatBadge CONSTANT FINAL)

public:
    explicit ThemeFullscreen(QObject* parent = nullptr);

    [[nodiscard]] static QColor fullscreenBackground();
    [[nodiscard]] static QColor fullscreenText();
    [[nodiscard]] static QColor fullscreenSecondaryText();
    [[nodiscard]] static QColor fullscreenTertiaryText();
    [[nodiscard]] static QColor fullscreenArtistText();
    [[nodiscard]] static QColor fullscreenMetaText();
    [[nodiscard]] static QColor fullscreenTimeText();
    [[nodiscard]] static QColor fullscreenEmptyText();
    [[nodiscard]] static QColor fullscreenButtonGlyph();
    [[nodiscard]] static QColor fullscreenButtonHover();
    [[nodiscard]] static QColor fullscreenRowHover();
    [[nodiscard]] static QColor fullscreenPlayFill();
    [[nodiscard]] static QColor fullscreenPlayGlyph();
    [[nodiscard]] static QColor fullscreenSeekFill();
    [[nodiscard]] static QColor fullscreenSeekTrack();
    [[nodiscard]] static QColor fullscreenTabsFill();
    [[nodiscard]] static QColor fullscreenTabsBorder();
    [[nodiscard]] static QColor fullscreenTabText();
    [[nodiscard]] static QColor fullscreenTabActiveFill();
    [[nodiscard]] static QColor fullscreenPanelFill();
    [[nodiscard]] static QColor fullscreenPanelBorder();
    [[nodiscard]] static QColor fullscreenBackdropEnd();
    [[nodiscard]] static QColor fullscreenBackdropDim();
    [[nodiscard]] static QColor fullscreenCaptionGlyph();

    [[nodiscard]] int fullscreenBackdropBlur() const { return 56; }
    [[nodiscard]] qreal fullscreenBackdropSaturation() const { return 1.2; }
    [[nodiscard]] qreal fullscreenBackdropScale() const { return 1.1; }
    [[nodiscard]] int fullscreenBackdropInset() const { return 60; }
    [[nodiscard]] int fullscreenArtRadius() const { return 20; }
    [[nodiscard]] int fullscreenPanelRadius() const { return 20; }
    [[nodiscard]] static QVariantList shadowFullscreenArt();
    [[nodiscard]] static QVariantList shadowFullscreenPlay();

    [[nodiscard]] QFont fullscreenArtist() const { return m_artist.font; }
    [[nodiscard]] qreal lineHeightFullscreenArtist() const { return m_artist.lineHeight; }
    [[nodiscard]] QFont fullscreenMeta() const { return m_meta.font; }
    [[nodiscard]] QFont fullscreenTime() const { return m_time.font; }
    [[nodiscard]] QFont fullscreenTab() const { return m_tab.font; }
    [[nodiscard]] QFont fullscreenTabActive() const { return m_tabActive.font; }
    [[nodiscard]] QFont fullscreenRepeatBadge() const { return m_repeatBadge.font; }

private:
    TextStyle m_artist;
    TextStyle m_meta;
    TextStyle m_time;
    TextStyle m_tab;
    TextStyle m_tabActive;
    TextStyle m_repeatBadge;
};

} // namespace lmp::ui
