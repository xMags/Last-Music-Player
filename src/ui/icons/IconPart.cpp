#include "ui/icons/IconPart.h"

#include <utility>

namespace lmp::ui {

namespace {

QColor resolve(IconPart::Paint paint, const QColor& fixed, const QColor& current)
{
    switch (paint) {
    case IconPart::Paint::None:
        return Qt::transparent;
    case IconPart::Paint::Current:
        return current;
    case IconPart::Paint::Fixed:
        return fixed;
    }
    return Qt::transparent;
}

} // namespace

IconPart::IconPart(QString path, Style style)
    : m_path(std::move(path))
    , m_style(std::move(style))
{
}

int IconPart::fillRule() const
{
    const bool winding = !m_style.evenOdd && m_style.keepWinding;
    return static_cast<int>(winding ? Qt::WindingFill : Qt::OddEvenFill);
}

QColor IconPart::fillFor(const QColor& current) const
{
    return resolve(m_style.fill, m_style.fillColor, current);
}

QColor IconPart::strokeFor(const QColor& current) const
{
    return resolve(m_style.stroke, m_style.strokeColor, current);
}

} // namespace lmp::ui
