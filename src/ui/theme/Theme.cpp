#include "ui/theme/Theme.h"

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

Theme::Theme(QObject* parent)
    : QObject(parent)
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

QVariantList Theme::artworkGradientList()
{
    QVariantList list;
    for (const auto& [start, end] : artworkGradients()) {
        list.append(QVariant::fromValue(QVariantList{start, end}));
    }
    return list;
}

QVariantList Theme::accentSeedList()
{
    QVariantList list;
    for (const BrandAccent& accent : brandAccents()) {
        list.append(accent.seed);
    }
    return list;
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
