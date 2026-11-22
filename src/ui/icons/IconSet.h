#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "ui/icons/IconData.h"
#include "ui/icons/IconPart.h"

namespace lmp::ui {

// Every icon of the design, by neutral name: outline icons, filled glyphs
// ("nav-<name>" with an "-active" variant, "solid-<name>") and device logos
// ("logo-browser", "logo-phone", "logo-desktop"). Icons are path data in a
// 24 x 24 view box, so they scale to any size without losing sharpness.
class IconSet : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Icons)
    QML_SINGLETON

    Q_PROPERTY(qreal viewBoxSize READ viewBoxSize CONSTANT FINAL)
    Q_PROPERTY(int maxParts READ maxParts CONSTANT FINAL)
    Q_PROPERTY(QStringList names READ names CONSTANT FINAL)

public:
    static constexpr qreal kViewBoxSize = 24;
    static constexpr int kMaxParts = 5;

    explicit IconSet(QObject* parent = nullptr);

    [[nodiscard]] qreal viewBoxSize() const { return kViewBoxSize; }
    // The most parts any icon has; the Icon component draws this many paths.
    [[nodiscard]] int maxParts() const { return kMaxParts; }
    [[nodiscard]] QStringList names() const { return m_names; }

    Q_INVOKABLE bool contains(const QString& name) const;
    Q_INVOKABLE int partCount(const QString& name) const;
    // An empty part when the name or the index is unknown.
    Q_INVOKABLE lmp::ui::IconPart part(const QString& name, int index) const;

private:
    void addTable(const std::vector<icondata::IconEntry>& table);
    void addIcon(const QString& name, QList<IconPart> parts);

    QHash<QString, QList<IconPart>> m_icons;
    QStringList m_names;
};

} // namespace lmp::ui
