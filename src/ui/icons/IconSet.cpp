#include "ui/icons/IconSet.h"

#include <QLatin1StringView>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPointF>

#include <utility>

namespace lmp::ui {

namespace {

IconPart::Paint toPaint(icondata::IconPaint paint)
{
    switch (paint) {
    case icondata::IconPaint::None:
        return IconPart::Paint::None;
    case icondata::IconPaint::Current:
        return IconPart::Paint::Current;
    case icondata::IconPaint::Fixed:
        return IconPart::Paint::Fixed;
    }
    return IconPart::Paint::None;
}

QColor toColor(const char* hex)
{
    return hex == nullptr ? QColor() : QColor::fromString(QLatin1StringView(hex));
}

// Icons with a nonzero part whose shape changes under the odd-even rule.
// tst_icon_set checks that every other nonzero part is the same under both.
bool keepsWinding(const QString& name)
{
    return name == QLatin1StringView("flame") || name == QLatin1StringView("logo-phone")
        || name == QLatin1StringView("download-done");
}

IconPart toPart(const icondata::IconPartData& data, bool keepWinding)
{
    return IconPart(QString::fromLatin1(data.path),
                    IconPart::Style{
                        .fill = toPaint(data.fill),
                        .fillColor = toColor(data.fillColor),
                        .stroke = toPaint(data.stroke),
                        .strokeColor = toColor(data.strokeColor),
                        .strokeWidth = data.strokeWidth,
                        .evenOdd = data.evenOdd,
                        .keepWinding = keepWinding,
                        .cap = data.cap,
                        .join = data.join,
                    });
}

QString svgNumber(qreal value)
{
    return QString::number(value, 'g', 6);
}

QString svgPoint(const QPointF& point)
{
    return svgNumber(point.x()) + QLatin1Char(' ') + svgNumber(point.y());
}

// Writes a QPainterPath as SVG path data (moveto, lineto, cubic curveto).
QString toSvgPath(const QPainterPath& path)
{
    QString data;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element element = path.elementAt(i);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            data += QLatin1Char('M') + svgPoint(element) + QLatin1Char(' ');
            break;
        case QPainterPath::LineToElement:
            data += QLatin1Char('L') + svgPoint(element) + QLatin1Char(' ');
            break;
        case QPainterPath::CurveToElement:
            if (i + 2 < path.elementCount()) {
                data += QLatin1Char('C') + svgPoint(element) + QLatin1Char(' ')
                    + svgPoint(path.elementAt(i + 1)) + QLatin1Char(' ')
                    + svgPoint(path.elementAt(i + 2)) + QLatin1Char(' ');
                i += 2;
            }
            break;
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    return data.trimmed();
}

// "download-done": a filled disc with a down arrow cut out of it. The design
// draws it with an SVG mask (a round-capped 1.9 wide arrow); Qt shapes have no
// masks, so the cut-out is computed once as geometry.
IconPart downloadDonePart()
{
    QPainterPath disc;
    disc.addEllipse(QPointF(12, 12), 10, 10);

    QPainterPath arrow;
    arrow.moveTo(12, 7.5);
    arrow.lineTo(12, 15.5);
    arrow.moveTo(8.5, 12.5);
    arrow.lineTo(12, 16);
    arrow.lineTo(15.5, 12.5);

    QPainterPathStroker stroker;
    stroker.setWidth(1.9);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    const QPainterPath cut = disc.subtracted(stroker.createStroke(arrow));

    return IconPart(toSvgPath(cut),
                    IconPart::Style{
                        .fill = IconPart::Paint::Current,
                        .evenOdd = cut.fillRule() == Qt::OddEvenFill,
                        .keepWinding = true,
                    });
}

} // namespace

IconSet::IconSet(QObject* parent)
    : QObject(parent)
{
    addTable(icondata::lineIcons());
    addTable(icondata::solidIcons());
    addTable(icondata::deviceLogos());
}

void IconSet::addTable(const std::vector<icondata::IconEntry>& table)
{
    for (const icondata::IconEntry& entry : table) {
        const QString name = QString::fromLatin1(entry.name);
        if (name == QLatin1StringView("download-done")) {
            addIcon(name, {downloadDonePart()});
            continue;
        }
        QList<IconPart> parts;
        parts.reserve(static_cast<qsizetype>(entry.parts.size()));
        for (const icondata::IconPartData& data : entry.parts) {
            parts.append(toPart(data, keepsWinding(name)));
        }
        addIcon(name, std::move(parts));
    }
}

void IconSet::addIcon(const QString& name, QList<IconPart> parts)
{
    Q_ASSERT_X(!m_icons.contains(name), "IconSet", "duplicate icon name");
    Q_ASSERT_X(parts.size() <= kMaxParts, "IconSet", "icon has more parts than Icon draws");
    m_names.append(name);
    m_icons.insert(name, std::move(parts));
}

bool IconSet::contains(const QString& name) const
{
    return m_icons.contains(name);
}

int IconSet::partCount(const QString& name) const
{
    const auto it = m_icons.constFind(name);
    return it == m_icons.cend() ? 0 : static_cast<int>(it->size());
}

IconPart IconSet::part(const QString& name, int index) const
{
    const auto it = m_icons.constFind(name);
    if (it == m_icons.cend() || index < 0 || index >= it->size()) {
        return {};
    }
    return it->at(index);
}

} // namespace lmp::ui
