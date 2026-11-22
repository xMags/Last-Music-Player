#include "ui/theme/ThemeTypography.h"

namespace lmp::ui {

ThemeTypography::ThemeTypography(QObject* parent)
    : QObject(parent)
    , m_families(FontFamilies::resolve())
    , m_text(TextStyles::build(m_families))
{
}

} // namespace lmp::ui
