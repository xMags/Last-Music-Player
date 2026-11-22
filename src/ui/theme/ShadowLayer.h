#pragma once

#include <QColor>
#include <QMetaType>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <initializer_list>

namespace lmp::ui {

// One layer of a CSS box-shadow: `offsetX offsetY blur spread color`. A token
// with several comma-separated layers becomes a list of these, drawn in order
// with one RectangularShadow each.
struct ShadowLayer {
    Q_GADGET
    QML_VALUE_TYPE(shadowLayer)

    Q_PROPERTY(qreal offsetX MEMBER offsetX CONSTANT FINAL)
    Q_PROPERTY(qreal offsetY MEMBER offsetY CONSTANT FINAL)
    Q_PROPERTY(qreal blur MEMBER blur CONSTANT FINAL)
    Q_PROPERTY(qreal spread MEMBER spread CONSTANT FINAL)
    Q_PROPERTY(QColor color MEMBER color CONSTANT FINAL)

public:
    qreal offsetX = 0;
    qreal offsetY = 0;
    qreal blur = 0;
    qreal spread = 0;
    QColor color;

    friend bool operator==(const ShadowLayer&, const ShadowLayer&) = default;
};

// Packs layers into the list form QML reads.
[[nodiscard]] QVariantList shadowLayers(std::initializer_list<ShadowLayer> layers);

} // namespace lmp::ui

Q_DECLARE_METATYPE(lmp::ui::ShadowLayer)
