#include "ui/navigation/FocusPlace.h"

#include <QAccessible>
#include <QList>
#include <QQuickItem>

namespace lmp::ui {

namespace {

[[nodiscard]] bool focusable(const QQuickItem* item)
{
    return item->isVisible() && item->isEnabled() && item->activeFocusOnTab();
}

// Every focusable control named `name` under `root`, in tree order.
void collect(QQuickItem* root, const QString& name, const FocusPlace& place, QList<QQuickItem*>& out)
{
    const QList<QQuickItem*> children = root->childItems();
    for (QQuickItem* child : children) {
        if (focusable(child) && place.nameOf(child) == name) {
            out.append(child);
        }
        collect(child, name, place, out);
    }
}

} // namespace

FocusPlace::FocusPlace(QObject* parent)
    : QObject(parent)
{
}

bool FocusPlace::hasKeyboardFocus(QQuickItem* item) const
{
    return item != nullptr && item->property("visualFocus").toBool();
}

QString FocusPlace::nameOf(QQuickItem* item) const
{
    if (item == nullptr) {
        return {};
    }
    QAccessibleInterface* accessible = QAccessible::queryAccessibleInterface(item);
    return accessible != nullptr ? accessible->text(QAccessible::Name) : QString();
}

int FocusPlace::indexOf(QQuickItem* root, QQuickItem* item) const
{
    if (root == nullptr || item == nullptr || !root->isAncestorOf(item)) {
        return -1;
    }
    const QString name = nameOf(item);
    if (name.isEmpty()) {
        return -1;
    }
    QList<QQuickItem*> matches;
    collect(root, name, *this, matches);
    return static_cast<int>(matches.indexOf(item));
}

QQuickItem* FocusPlace::find(QQuickItem* root, const QString& name, int index) const
{
    if (root == nullptr || name.isEmpty()) {
        return nullptr;
    }
    QList<QQuickItem*> matches;
    collect(root, name, *this, matches);
    if (matches.isEmpty()) {
        return nullptr;
    }
    return index >= 0 && index < matches.size() ? matches.at(index) : matches.first();
}

} // namespace lmp::ui
