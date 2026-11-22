#include "ui/theme/Theme.h"

#include <QGuiApplication>
#include <QStyleHints>

#include "ui/theme/ColorMix.h"
#include "ui/theme/ShadowLayer.h"

namespace lmp::ui {

namespace {

QColor black(float alpha)
{
    QColor color(Qt::black);
    color.setAlphaF(alpha);
    return color;
}

bool systemPrefersDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace

Theme::Theme(QObject* parent)
    : ThemeConstants(parent)
    , m_systemDark(systemPrefersDark())
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme scheme) {
                const bool systemDark = scheme == Qt::ColorScheme::Dark;
                if (systemDark == m_systemDark) {
                    return;
                }
                m_systemDark = systemDark;
                if (m_mode == ThemeMode::System) {
                    emit changed();
                }
            });
}

void Theme::setMode(ThemeMode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    emit changed();
}

void Theme::setAccent(Accent accent)
{
    if (accent == m_accent) {
        return;
    }
    m_accent = accent;
    emit changed();
}

bool Theme::dark() const
{
    switch (m_mode) {
    case ThemeMode::Light:
        return false;
    case ThemeMode::Dark:
        return true;
    case ThemeMode::System:
        return m_systemDark;
    }
    return false;
}

QColor Theme::primary() const
{
    return dark() ? currentAccent().dark : currentAccent().light;
}

// The accent-soft family mixes the primary colour with transparent in sRGB.
QColor Theme::accentSoft() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.12);
}

QColor Theme::accentSoft2() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.20);
}

QColor Theme::accentSel() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.08);
}

// The only alias mixed in OKLab: the seed lightened towards white.
QColor Theme::accentFill2() const
{
    return lmp::ui::mixOklab(accentSeed(), Qt::white, 0.72);
}

QVariantList Theme::shadow() const
{
    return shadowLayers({{0, 1, 3, 0, black(dark() ? 0.32f : 0.06f)}});
}

QVariantList Theme::shadowCard() const
{
    return shadowLayers({{0, 2, 6, 0, black(dark() ? 0.36f : 0.07f)}});
}

// The same in both themes.
QVariantList Theme::shadowFlyout() const
{
    return shadowLayers({{0, 8, 16, 0, black(0.10f)}, {0, 0, 1, 0, black(0.20f)}});
}

QVariantList Theme::shadowArt() const
{
    if (dark()) {
        return shadowLayers({{0, 2, 4, 0, black(0.40f)},
                             {0, 16, 40, 0, black(0.50f)},
                             {0, 28, 60, 0, black(0.35f)}});
    }
    return shadowLayers({{0, 2, 4, 0, black(0.06f)},
                         {0, 12, 28, 0, black(0.14f)},
                         {0, 24, 48, 0, black(0.10f)}});
}

// The accent button glow follows the accent's seed colour.
QVariantList Theme::shadowButton() const
{
    return shadowLayers({{0, 1, 2, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.24)},
                         {0, 8, 22, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.34)}});
}

QVariantList Theme::shadowButtonHover() const
{
    return shadowLayers({{0, 2, 4, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.28)},
                         {0, 14, 32, 0, lmp::ui::mixSrgb(accentSeed(), Qt::transparent, 0.42)}});
}

QColor Theme::mixSrgb(const QColor& a, const QColor& b, qreal amountOfA) const
{
    return lmp::ui::mixSrgb(a, b, amountOfA);
}

QColor Theme::mixOklab(const QColor& a, const QColor& b, qreal amountOfA) const
{
    return lmp::ui::mixOklab(a, b, amountOfA);
}

const ThemePalette& Theme::palette() const
{
    return dark() ? m_dark : m_light;
}

const BrandAccent& Theme::currentAccent() const
{
    const auto index = static_cast<std::size_t>(m_accent);
    return brandAccents().at(index);
}

} // namespace lmp::ui
