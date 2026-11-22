#pragma once

#include <QColor>
#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "ui/theme/ThemeConstants.h"
#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

// Every colour of the design system for the current theme and accent. All
// colour properties share the one `changed` signal, so switching theme or
// accent updates every binding at once. Names follow the design token names.
// Sizes and other values that never change come from ThemeConstants.
class Theme : public ThemeConstants {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(ThemeMode mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(Accent accent READ accent WRITE setAccent NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)

    // Design tokens.
    Q_PROPERTY(QColor accentSeed READ accentSeed NOTIFY changed)
    Q_PROPERTY(QColor accentLight READ accentLight NOTIFY changed)
    Q_PROPERTY(QColor accentDark READ accentDark NOTIFY changed)
    Q_PROPERTY(QColor primary READ primary NOTIFY changed)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceAlt READ surfaceAlt NOTIFY changed)
    Q_PROPERTY(QColor canvas READ canvas NOTIFY changed)
    Q_PROPERTY(QColor neutral READ neutral NOTIFY changed)
    Q_PROPERTY(QColor grad1 READ grad1 NOTIFY changed)
    Q_PROPERTY(QColor grad2 READ grad2 NOTIFY changed)
    Q_PROPERTY(QColor stroke READ stroke NOTIFY changed)
    Q_PROPERTY(QColor strokeStrong READ strokeStrong NOTIFY changed)
    Q_PROPERTY(QColor divider READ divider NOTIFY changed)
    Q_PROPERTY(QColor rowHover READ rowHover NOTIFY changed)
    Q_PROPERTY(QColor controlHover READ controlHover NOTIFY changed)
    Q_PROPERTY(QColor segmentTrack READ segmentTrack NOTIFY changed)
    Q_PROPERTY(QColor segmentSelected READ segmentSelected NOTIFY changed)
    Q_PROPERTY(QColor fg1 READ fg1 NOTIFY changed)
    Q_PROPERTY(QColor fg2 READ fg2 NOTIFY changed)
    Q_PROPERTY(QColor fg3 READ fg3 NOTIFY changed)
    Q_PROPERTY(QColor fg4 READ fg4 NOTIFY changed)
    Q_PROPERTY(QColor playBg READ playBg NOTIFY changed)
    Q_PROPERTY(QColor playFg READ playFg NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)

    // Aliases the screens use.
    Q_PROPERTY(QColor accentColor READ primary NOTIFY changed)
    Q_PROPERTY(QColor accentInk READ onPrimary NOTIFY changed)
    Q_PROPERTY(QColor accentSoft READ accentSoft NOTIFY changed)
    Q_PROPERTY(QColor accentSoft2 READ accentSoft2 NOTIFY changed)
    Q_PROPERTY(QColor accentSel READ accentSel NOTIFY changed)
    Q_PROPERTY(QColor accentFill READ accentSeed NOTIFY changed)
    Q_PROPERTY(QColor accentFill2 READ accentFill2 NOTIFY changed)
    Q_PROPERTY(QColor bg READ canvas NOTIFY changed)
    Q_PROPERTY(QColor surfaceElev READ surface NOTIFY changed)
    Q_PROPERTY(QColor fg READ fg1 NOTIFY changed)
    Q_PROPERTY(QColor fgSecondary READ fg2 NOTIFY changed)
    Q_PROPERTY(QColor fgTertiary READ fg3 NOTIFY changed)
    Q_PROPERTY(QColor fgDisabled READ fg4 NOTIFY changed)
    Q_PROPERTY(QColor hover READ controlHover NOTIFY changed)
    Q_PROPERTY(QColor panelGrad1 READ grad1 NOTIFY changed)
    Q_PROPERTY(QColor panelGrad2 READ grad2 NOTIFY changed)
    Q_PROPERTY(QColor artGrad1 READ artGrad1 NOTIFY changed)
    Q_PROPERTY(QColor artGrad2 READ artGrad2 NOTIFY changed)

    // Box shadows: lists of ShadowLayer, drawn in order.
    Q_PROPERTY(QVariantList shadow READ shadow NOTIFY changed)
    Q_PROPERTY(QVariantList shadowCard READ shadowCard NOTIFY changed)
    Q_PROPERTY(QVariantList shadowFlyout READ shadowFlyout NOTIFY changed)
    Q_PROPERTY(QVariantList shadowArt READ shadowArt NOTIFY changed)
    Q_PROPERTY(QVariantList shadowButton READ shadowButton NOTIFY changed)
    Q_PROPERTY(QVariantList shadowButtonHover READ shadowButtonHover NOTIFY changed)

public:
    enum class ThemeMode { Light, Dark, System };
    Q_ENUM(ThemeMode)

    enum class Accent { Teal, Purple, Coral, Green, Amber };
    Q_ENUM(Accent)

    explicit Theme(QObject* parent = nullptr);

    [[nodiscard]] ThemeMode mode() const { return m_mode; }
    void setMode(ThemeMode mode);
    [[nodiscard]] Accent accent() const { return m_accent; }
    void setAccent(Accent accent);
    [[nodiscard]] bool dark() const;

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
    [[nodiscard]] QColor artGrad1() const { return palette().artGrad1; }
    [[nodiscard]] QColor artGrad2() const { return palette().artGrad2; }
    [[nodiscard]] QColor accentSoft() const;
    [[nodiscard]] QColor accentSoft2() const;
    [[nodiscard]] QColor accentSel() const;
    [[nodiscard]] QColor accentFill2() const;

    [[nodiscard]] QVariantList shadow() const;
    [[nodiscard]] QVariantList shadowCard() const;
    [[nodiscard]] QVariantList shadowFlyout() const;
    [[nodiscard]] QVariantList shadowArt() const;
    [[nodiscard]] QVariantList shadowButton() const;
    [[nodiscard]] QVariantList shadowButtonHover() const;

    Q_INVOKABLE QColor mixSrgb(const QColor& a, const QColor& b, qreal amountOfA) const;
    Q_INVOKABLE QColor mixOklab(const QColor& a, const QColor& b, qreal amountOfA) const;

signals:
    void changed();

private:
    [[nodiscard]] const ThemePalette& palette() const;
    [[nodiscard]] const BrandAccent& currentAccent() const;

    ThemeMode m_mode = ThemeMode::System;
    Accent m_accent = Accent::Teal;
    bool m_systemDark = false;
    ThemePalette m_light = ThemePalette::light();
    ThemePalette m_dark = ThemePalette::dark();
};

} // namespace lmp::ui
