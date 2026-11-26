#include "ui/icons/IconImageProvider.h"

#include "ui/icons/IconData.h"

#include <QByteArray>
#include <QColor>
#include <QPainter>
#include <QStringList>
#include <QSvgRenderer>

#include <cstring>
#include <vector>

namespace lmp::ui {

namespace {

// The default size when the image asks for none: the icons' view box.
constexpr int kDefaultSide = 24;

[[nodiscard]] const icondata::IconEntry* findIcon(const QByteArray& name)
{
    for (const std::vector<icondata::IconEntry>* table :
         {&icondata::lineIcons(), &icondata::solidIcons(), &icondata::deviceLogos()}) {
        for (const icondata::IconEntry& entry : *table) {
            if (name == entry.name) {
                return &entry;
            }
        }
    }
    return nullptr;
}

[[nodiscard]] QByteArray capName(Qt::PenCapStyle cap)
{
    switch (cap) {
    case Qt::RoundCap:
        return QByteArrayLiteral("round");
    case Qt::SquareCap:
        return QByteArrayLiteral("square");
    default:
        return QByteArrayLiteral("butt");
    }
}

[[nodiscard]] QByteArray joinName(Qt::PenJoinStyle join)
{
    switch (join) {
    case Qt::RoundJoin:
        return QByteArrayLiteral("round");
    case Qt::BevelJoin:
        return QByteArrayLiteral("bevel");
    default:
        return QByteArrayLiteral("miter");
    }
}

[[nodiscard]] QByteArray paintOf(icondata::IconPaint paint, const char* fixed, const QByteArray& current)
{
    switch (paint) {
    case icondata::IconPaint::Current:
        return current;
    case icondata::IconPaint::Fixed:
        return fixed != nullptr ? QByteArray(fixed) : current;
    case icondata::IconPaint::None:
        break;
    }
    return QByteArrayLiteral("none");
}

} // namespace

IconImageProvider::IconImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QByteArray IconImageProvider::svgFor(const QString& id)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    if (parts.size() < 2) {
        return {};
    }
    const icondata::IconEntry* icon = findIcon(parts.at(0).toLatin1());
    const QColor color(QLatin1Char('#') + parts.at(1));
    if (icon == nullptr || !color.isValid()) {
        return {};
    }
    const bool outline = parts.size() > 2 && parts.at(2) == QLatin1StringView("outline");
    const QByteArray current = color.name(QColor::HexRgb).toLatin1();
    QByteArray svg = QByteArrayLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">)");
    for (const icondata::IconPartData& part : icon->parts) {
        QByteArray fill = paintOf(part.fill, part.fillColor, current);
        QByteArray stroke = paintOf(part.stroke, part.strokeColor, current);
        if (outline && fill != "none") {
            stroke = fill;
            fill = QByteArrayLiteral("none");
        }
        svg += QByteArrayLiteral(R"(<path d=")") + QByteArray(part.path) + QByteArrayLiteral(R"(" fill=")") + fill
            + QByteArrayLiteral(R"(" stroke=")") + stroke + QByteArrayLiteral(R"(" stroke-width=")")
            + QByteArray::number(part.strokeWidth) + QByteArrayLiteral(R"(" stroke-linecap=")") + capName(part.cap)
            + QByteArrayLiteral(R"(" stroke-linejoin=")") + joinName(part.join)
            + QByteArrayLiteral(R"(" fill-rule=")")
            + (part.evenOdd ? QByteArrayLiteral("evenodd") : QByteArrayLiteral("nonzero")) + QByteArrayLiteral(R"("/>)");
    }
    svg += QByteArrayLiteral("</svg>");
    return svg;
}

QImage IconImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QByteArray svg = svgFor(id);
    if (svg.isEmpty()) {
        return {};
    }
    const QSize side = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(kDefaultSide, kDefaultSide);
    QImage image(side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        return {};
    }
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    if (size != nullptr) {
        *size = image.size();
    }
    return image;
}

} // namespace lmp::ui
