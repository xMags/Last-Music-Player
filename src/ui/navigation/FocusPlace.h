#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQuickItem;

namespace lmp::ui {

// Finds a page's focused control again after the page was rebuilt (Back):
// by its accessible name and its place among the page's focusable controls
// of that name, in tree order (view-place.ts keeps a card's or row's key and
// index the same way).
//
// GUI thread only.
class FocusPlace : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.focusPlace")

public:
    explicit FocusPlace(QObject* parent = nullptr);

    // True when `item` is a control that got its focus from the keyboard
    // (its visualFocus), so a click never moves the remembered place.
    Q_INVOKABLE bool hasKeyboardFocus(QQuickItem* item) const;
    // The name screen readers hear for `item` (empty when it has none).
    Q_INVOKABLE QString nameOf(QQuickItem* item) const;
    // `item`'s place among the focusable controls under `root` with its name,
    // or -1 when it is not under `root` or has no name.
    Q_INVOKABLE int indexOf(QQuickItem* root, QQuickItem* item) const;
    // The `index`th focusable control named `name` under `root` (the first
    // one when there are fewer), or nullptr.
    Q_INVOKABLE QQuickItem* find(QQuickItem* root, const QString& name, int index) const;
};

} // namespace lmp::ui
