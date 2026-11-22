#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>
#include <QtCore/qnamespace.h>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// One drawable part of an icon, as QML reads it: path data plus paint. The
// colour methods resolve "the icon's colour" so a ShapePath binding stays a
// single call. A default-constructed part draws nothing.
class IconPart {
    Q_GADGET
    QML_VALUE_TYPE(iconPart)

    Q_PROPERTY(QString path READ path CONSTANT FINAL)
    Q_PROPERTY(qreal strokeWidth READ strokeWidth CONSTANT FINAL)
    Q_PROPERTY(int fillRule READ fillRule CONSTANT FINAL)
    Q_PROPERTY(int capStyle READ capStyle CONSTANT FINAL)
    Q_PROPERTY(int joinStyle READ joinStyle CONSTANT FINAL)

public:
    enum class Paint { None, Current, Fixed };

    struct Style {
        Paint fill = Paint::None;
        QColor fillColor;
        Paint stroke = Paint::None;
        QColor strokeColor;
        qreal strokeWidth = 1;
        // The SVG fill-rule of the source: evenodd when true, nonzero otherwise.
        bool evenOdd = false;
        // Draw a nonzero part with the winding rule. Only set where the two
        // rules give different shapes; see fillRule().
        bool keepWinding = false;
        Qt::PenCapStyle cap = Qt::FlatCap;
        Qt::PenJoinStyle join = Qt::MiterJoin;
    };

    IconPart() = default;
    IconPart(QString path, Style style);

    [[nodiscard]] QString path() const { return m_path; }
    [[nodiscard]] qreal strokeWidth() const { return m_style.strokeWidth; }
    // The rule the part is drawn with. Qt's curve renderer drops parts of
    // some shapes filled with the winding rule, so nonzero parts are drawn
    // with the odd-even rule wherever both rules give the same shape.
    [[nodiscard]] int fillRule() const;
    [[nodiscard]] bool sourceEvenOdd() const { return m_style.evenOdd; }
    [[nodiscard]] int capStyle() const { return static_cast<int>(m_style.cap); }
    [[nodiscard]] int joinStyle() const { return static_cast<int>(m_style.join); }
    [[nodiscard]] bool filled() const { return m_style.fill != Paint::None; }
    [[nodiscard]] bool stroked() const { return m_style.stroke != Paint::None; }

    // The colour to fill or stroke with when the icon is drawn in `current`.
    // Transparent when this part is not filled or not stroked.
    Q_INVOKABLE QColor fillFor(const QColor& current) const;
    Q_INVOKABLE QColor strokeFor(const QColor& current) const;

private:
    QString m_path;
    Style m_style;
};

} // namespace lmp::ui

Q_DECLARE_METATYPE(lmp::ui::IconPart)
