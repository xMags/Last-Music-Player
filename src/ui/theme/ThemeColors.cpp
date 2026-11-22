#include "ui/theme/ThemeColors.h"

#include <QGuiApplication>
#include <QStyleHints>

#include "ui/theme/ColorMix.h"

namespace lmp::ui {

namespace {

bool systemPrefersDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

} // namespace

ThemeColors::ThemeColors(QObject* parent)
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


void ThemeColors::setMode(ThemeMode mode)
{
    if (mode == m_mode) {
        return;
    }
    m_mode = mode;
    emit changed();
}

void ThemeColors::setAccent(Accent accent)
{
    if (accent == m_accent) {
        return;
    }
    m_accent = accent;
    emit changed();
}

bool ThemeColors::dark() const
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

QColor ThemeColors::primary() const
{
    return dark() ? currentAccent().dark : currentAccent().light;
}

// The accent-soft family mixes the primary colour with transparent in sRGB.
QColor ThemeColors::accentSoft() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.12);
}

QColor ThemeColors::accentSoft2() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.20);
}

QColor ThemeColors::accentSel() const
{
    return lmp::ui::mixSrgb(primary(), Qt::transparent, 0.08);
}

// The only alias mixed in OKLab: the seed lightened towards white.
QColor ThemeColors::accentFill2() const
{
    return lmp::ui::mixOklab(accentSeed(), Qt::white, 0.72);
}

QColor ThemeColors::mixSrgb(const QColor& a, const QColor& b, qreal amountOfA) const
{
    return lmp::ui::mixSrgb(a, b, amountOfA);
}

QColor ThemeColors::mixOklab(const QColor& a, const QColor& b, qreal amountOfA) const
{
    return lmp::ui::mixOklab(a, b, amountOfA);
}

const ThemePalette& ThemeColors::palette() const
{
    return dark() ? m_dark : m_light;
}

const BrandAccent& ThemeColors::currentAccent() const
{
    const auto index = static_cast<std::size_t>(m_accent);
    return brandAccents().at(index);
}

} // namespace lmp::ui
