#include "ui/theme/ThemeConstants.h"

#include <QVariant>

#include "ui/theme/ThemePalette.h"

namespace lmp::ui {

ThemeConstants::ThemeConstants(QObject* parent)
    : QObject(parent)
    , m_families(FontFamilies::resolve())
    , m_text(TextStyles::build(m_families))
{
}

QVariantList ThemeConstants::artworkGradientList()
{
    QVariantList list;
    for (const auto& [start, end] : artworkGradients()) {
        list.append(QVariant::fromValue(QVariantList{start, end}));
    }
    return list;
}

QVariantList ThemeConstants::accentSeedList()
{
    QVariantList list;
    for (const BrandAccent& accent : brandAccents()) {
        list.append(accent.seed);
    }
    return list;
}

} // namespace lmp::ui
