#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// A genre's drawn tile for QML (core::resolveGenreArtworkRecipe): the
// gradient's two colours and its marks, each a map of kind, x, y, width,
// height, stroke, radius, rotation and dashed, in the 96-unit mark box.
class GenreArtwork : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit GenreArtwork(QObject* parent = nullptr);

    // { start, end, marks }
    Q_INVOKABLE QVariantMap recipe(const QString& genre) const;
};

} // namespace lmp::ui
