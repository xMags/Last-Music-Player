#include "ui/effects/EffectOwnership.h"

#include <QVariant>

namespace lmp::ui {

namespace {

void own(QQuickItem* owner, QQuickItem* item)
{
    if (item != nullptr && item != owner && item->parent() == nullptr) {
        item->setParent(owner);
    }
}

void adoptUnowned(QQuickItem* owner)
{
    // RectangularShadow's default material, attached or not yet.
    own(owner, owner->property("material").value<QQuickItem*>());
    const QList<QQuickItem*> children = owner->childItems();
    for (QQuickItem* child : children) {
        own(owner, child);
    }
}

} // namespace

EffectOwnership::EffectOwnership(QObject* parent)
    : QObject(parent)
{
}

void EffectOwnership::adopt(QQuickItem* item) const
{
    if (item == nullptr) {
        return;
    }
    adoptUnowned(item);
    // The connection ends with the item; the lambda only touches the item.
    connect(item, &QQuickItem::childrenChanged, item, [item]() { adoptUnowned(item); });
}

} // namespace lmp::ui
