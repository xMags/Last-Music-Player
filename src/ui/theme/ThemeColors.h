#pragma once

#include <QColor>
#include <QObject>

#include "ui/theme/ThemeConstants.h"
#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

// The design token colours for the current theme mode and accent. All
// colour properties share the one `changed` signal, so switching theme or
// accent updates every binding at once. Names follow the design token names.
// Part of the Theme singleton, which adds the colours derived from these.
class ThemeColors : public ThemeConstants {
    Q_OBJECT

    Q_PROPERTY(ThemeMode mode READ mode WRITE setMode NOTIFY changed FINAL)
    Q_PROPERTY(Accent accent READ accent WRITE setAccent NOTIFY changed FINAL)
    Q_PROPERTY(bool dark READ dark NOTIFY changed FINAL)
    // Windows high contrast is on: the colours are the system's.
    Q_PROPERTY(bool highContrast READ highContrast NOTIFY changed FINAL)

    // Design tokens.
    Q_PROPERTY(QColor accentSeed READ accentSeed NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentLight READ accentLight NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentDark READ accentDark NOTIFY changed FINAL)
    Q_PROPERTY(QColor primary READ primary NOTIFY changed FINAL)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed FINAL)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed FINAL)
    Q_PROPERTY(QColor canvas READ canvas NOTIFY changed FINAL)
    Q_PROPERTY(QColor neutral READ neutral NOTIFY changed FINAL)
    Q_PROPERTY(QColor grad1 READ grad1 NOTIFY changed FINAL)
    Q_PROPERTY(QColor grad2 READ grad2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor stroke READ stroke NOTIFY changed FINAL)
    Q_PROPERTY(QColor strokeStrong READ strokeStrong NOTIFY changed FINAL)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed FINAL)
    Q_PROPERTY(QColor rowHover READ rowHover NOTIFY changed FINAL)
    Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed FINAL)
    Q_PROPERTY(QColor segmentTrack READ segmentTrack NOTIFY changed FINAL)
    Q_PROPERTY(QColor segmentSelected READ segmentSelected NOTIFY changed FINAL)
    Q_PROPERTY(QColor fg1 READ fg1 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fg2 READ fg2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fg3 READ fg3 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fg4 READ fg4 NOTIFY changed FINAL)
    Q_PROPERTY(QColor playBg READ playBg NOTIFY changed FINAL)
    Q_PROPERTY(QColor playFg READ playFg NOTIFY changed FINAL)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed FINAL)
    // The confirm button of a Settings dialog (.settings-confirm-danger):
    // its hover fill and its text.
    Q_PROPERTY(QColor dangerConfirmHover READ dangerConfirmHover NOTIFY changed FINAL)
    Q_PROPERTY(QColor dangerConfirmInk READ dangerConfirmInk NOTIFY changed FINAL)

    // Aliases the screens use.
    Q_PROPERTY(QColor accentColor READ primary NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentInk READ onPrimary NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentSoft2 READ accentSoft2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentSel READ accentSel NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentFill READ accentSeed NOTIFY changed FINAL)
    Q_PROPERTY(QColor accentFill2 READ accentFill2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor bg READ canvas NOTIFY changed FINAL)
    Q_PROPERTY(QColor surfaceElev READ surface NOTIFY changed FINAL)
    Q_PROPERTY(QColor fg READ fg1 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fgSecondary READ fg2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fgTertiary READ fg3 NOTIFY changed FINAL)
    Q_PROPERTY(QColor fgDisabled READ fg4 NOTIFY changed FINAL)
    Q_PROPERTY(QColor hover READ controlHover NOTIFY changed FINAL)
    Q_PROPERTY(QColor panelGrad1 READ grad1 NOTIFY changed FINAL)
    Q_PROPERTY(QColor panelGrad2 READ grad2 NOTIFY changed FINAL)
    Q_PROPERTY(QColor artGrad1 READ artGrad1 NOTIFY changed FINAL)
    Q_PROPERTY(QColor artGrad2 READ artGrad2 NOTIFY changed FINAL)

public:
    enum class ThemeMode { Light, Dark, System };
    Q_ENUM(ThemeMode)

    enum class Accent { Teal, Purple, Coral, Green, Amber };
    Q_ENUM(Accent)

    explicit ThemeColors(QObject* parent = nullptr);

    [[nodiscard]] ThemeMode mode() const { return m_mode; }
    void setMode(ThemeMode mode);
    [[nodiscard]] Accent accent() const { return m_accent; }
    void setAccent(Accent accent);
    [[nodiscard]] bool dark() const;
    [[nodiscard]] bool highContrast() const { return m_highContrast; }

    [[nodiscard]] QColor accentSeed() const { return currentAccent().seed; }
    [[nodiscard]] QColor accentLight() const { return currentAccent().light; }
    [[nodiscard]] QColor accentDark() const { return currentAccent().dark; }
    [[nodiscard]] QColor primary() const;
    [[nodiscard]] QColor onPrimary() const { return palette().onPrimary; }
    [[nodiscard]] QColor surface() const { return palette().surface; }
    [[nodiscard]] QColor surfaceAlt() const { return palette().surfaceAlt; }
    [[nodiscard]] QColor canvas() const { return palette().canvas; }
    [[nodiscard]] QColor neutral() const { return palette().neutral; }
    [[nodiscard]] QColor grad1() const { return palette().grad1; }
    [[nodiscard]] QColor grad2() const { return palette().grad2; }
    [[nodiscard]] QColor stroke() const { return palette().stroke; }
    [[nodiscard]] QColor strokeStrong() const { return palette().strokeStrong; }
    [[nodiscard]] QColor divider() const { return palette().divider; }
    [[nodiscard]] QColor rowHover() const { return palette().rowHover; }
    [[nodiscard]] QColor controlHover() const { return palette().controlHover; }
    [[nodiscard]] QColor segmentTrack() const { return palette().segmentTrack; }
    [[nodiscard]] QColor segmentSelected() const { return palette().segmentSelected; }
    [[nodiscard]] QColor fg1() const { return palette().fg1; }
    [[nodiscard]] QColor fg2() const { return palette().fg2; }
    [[nodiscard]] QColor fg3() const { return palette().fg3; }
    [[nodiscard]] QColor fg4() const { return palette().fg4; }
    [[nodiscard]] QColor playBg() const { return palette().playBg; }
    [[nodiscard]] QColor playFg() const { return palette().playFg; }
    [[nodiscard]] QColor danger() const { return palette().danger; }
    [[nodiscard]] QColor dangerConfirmHover() const;
    [[nodiscard]] QColor dangerConfirmInk() const;
    [[nodiscard]] QColor artGrad1() const { return palette().artGrad1; }
    [[nodiscard]] QColor artGrad2() const { return palette().artGrad2; }
    [[nodiscard]] QColor accentSoft() const;
    [[nodiscard]] QColor accentSoft2() const;
    [[nodiscard]] QColor accentSel() const;
    [[nodiscard]] QColor accentFill2() const;

    Q_INVOKABLE QColor mixSrgb(const QColor& a, const QColor& b, qreal amountOfA) const;
    Q_INVOKABLE QColor mixOklab(const QColor& a, const QColor& b, qreal amountOfA) const;
    // CSS filter: brightness(`factor`): each channel times it, clipped.
    Q_INVOKABLE QColor brightness(const QColor& color, qreal factor) const;

signals:
    void changed();


protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    [[nodiscard]] const ThemePalette& palette() const;
    [[nodiscard]] const BrandAccent& currentAccent() const;

    ThemeMode m_mode = ThemeMode::System;
    Accent m_accent = Accent::Teal;
    bool m_systemDark = false;
    bool m_highContrast = false;
    ThemePalette m_contrast;
    ThemePalette m_light = ThemePalette::light();
    ThemePalette m_dark = ThemePalette::dark();
};

} // namespace lmp::ui
