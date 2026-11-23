#include "core/settings/AppearanceSettings.h"

#include <QLatin1StringView>

#include <array>
#include <cstddef>

namespace lmp::core::settings {

namespace {

// The colours earlier versions offered, in picker order. These are what is
// saved, so the earlier versions' own picker still marks the choice.
constexpr std::array<QLatin1StringView, AccentChoiceCount> kStoredAccents{
    QLatin1StringView("#FF0097B2"), QLatin1StringView("#FF6B8EFE"), QLatin1StringView("#FFE0719C"),
    QLatin1StringView("#FF6FBF73"), QLatin1StringView("#FFE8A33D"),
};

// The picker's seed colours (the design tokens' brand accents), as RRGGBB.
constexpr std::array<QLatin1StringView, AccentChoiceCount> kSeedAccents{
    QLatin1StringView("0097B2"), QLatin1StringView("6A5CD6"), QLatin1StringView("E0574A"),
    QLatin1StringView("1F9D6B"), QLatin1StringView("D88A2E"),
};

// The RRGGBB part of "#RRGGBB" or "#AARRGGBB" in upper case, or nothing when
// the text is neither.
QString rgbPart(const QString& stored)
{
    QString text = stored.trimmed().toUpper();
    if (text.startsWith(QLatin1Char('#'))) {
        text.remove(0, 1);
    }
    if (text.size() == 8) {
        text.remove(0, 2);
    }
    if (text.size() != 6) {
        return {};
    }
    for (const QChar c : text) {
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
        if (!hex) {
            return {};
        }
    }
    return text;
}

} // namespace

ThemeChoice themeChoiceFromStored(const QString& stored)
{
    if (stored == QLatin1StringView("Light")) {
        return ThemeChoice::Light;
    }
    if (stored == QLatin1StringView("Dark")) {
        return ThemeChoice::Dark;
    }
    return ThemeChoice::System;
}

QString storedThemeChoice(ThemeChoice choice)
{
    switch (choice) {
    case ThemeChoice::Light:
        return QStringLiteral("Light");
    case ThemeChoice::Dark:
        return QStringLiteral("Dark");
    case ThemeChoice::System:
        break;
    }
    return QStringLiteral("System");
}

int accentChoiceFromStored(const QString& stored)
{
    const QString rgb = rgbPart(stored);
    if (rgb.isEmpty()) {
        return 0;
    }
    for (int index = 0; index < AccentChoiceCount; ++index) {
        const auto slot = static_cast<std::size_t>(index);
        if (rgb == kStoredAccents[slot].sliced(3) || rgb == kSeedAccents[slot]) {
            return index;
        }
    }
    return 0;
}

QString storedAccentChoice(int index)
{
    if (index < 0 || index >= AccentChoiceCount) {
        index = 0;
    }
    return QString(kStoredAccents[static_cast<std::size_t>(index)]);
}

} // namespace lmp::core::settings
