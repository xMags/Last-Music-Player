#pragma once

#include <QObject>
#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// Makes an item the owner of the item it draws with, when Qt left that
// without one.
//
// RectangularShadow (Qt Quick Effects, 6.10) creates its default material, a
// ShaderEffect, in its private constructor before the shadow's own pointer
// is set, so the material has no QObject parent; it is later attached only
// as a child item. Destroying the shadow detaches it but never deletes it,
// so every shadow destroyed left a ShaderEffect (and its GPU resources)
// behind: Home, whose cards carry one shadow per shadow layer and are rebuilt
// on each visit, grew by megabytes every time.
//
// adopt() gives the item's `material`, and any child item without an owner
// (now and whenever its children change), the item as QObject parent, so
// they are deleted with it. Items that already have an owner are left alone.
//
// GUI thread only. Used through AppRectangularShadow.qml.
class EffectOwnership : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit EffectOwnership(QObject* parent = nullptr);

    Q_INVOKABLE void adopt(QQuickItem* item) const;
};

} // namespace lmp::ui
