#include "ui/library/GenreArtwork.h"

#include <QColor>
#include <QVariantList>

#include "core/library/GenreArtworkRecipe.h"

namespace lmp::ui {

namespace {

QString kindName(core::GenreArtworkPrimitiveKind kind)
{
    using Kind = core::GenreArtworkPrimitiveKind;
    switch (kind) {
    case Kind::Dot:
        return QStringLiteral("dot");
    case Kind::Ring:
        return QStringLiteral("ring");
    case Kind::Oval:
        return QStringLiteral("oval");
    case Kind::Bar:
        return QStringLiteral("bar");
    case Kind::OutlineBox:
        return QStringLiteral("outlineBox");
    case Kind::FilledSquare:
        return QStringLiteral("filledSquare");
    case Kind::OutlineRectangle:
        return QStringLiteral("outlineRectangle");
    case Kind::Column:
        return QStringLiteral("column");
    case Kind::Arch:
        return QStringLiteral("arch");
    case Kind::Triangle:
        return QStringLiteral("triangle");
    }
    return QStringLiteral("dot");
}

} // namespace

GenreArtwork::GenreArtwork(QObject* parent)
    : QObject(parent)
{
}

QVariantMap GenreArtwork::recipe(const QString& genre) const
{
    const core::GenreArtworkRecipe& recipe = core::resolveGenreArtworkRecipe(genre);
    QVariantList marks;
    for (const core::GenreArtworkPrimitive& mark : recipe.primitives) {
        marks.append(QVariantMap{
            {QStringLiteral("kind"), kindName(mark.kind)},
            {QStringLiteral("x"), mark.x},
            {QStringLiteral("y"), mark.y},
            {QStringLiteral("width"), mark.width},
            {QStringLiteral("height"), mark.height},
            {QStringLiteral("stroke"), mark.strokeWidth},
            {QStringLiteral("radius"), mark.cornerRadius},
            {QStringLiteral("rotation"), mark.rotation},
            {QStringLiteral("dashed"), mark.dashed},
        });
    }
    return {
        {QStringLiteral("start"), QColor::fromRgba(recipe.gradientStart)},
        {QStringLiteral("end"), QColor::fromRgba(recipe.gradientEnd)},
        {QStringLiteral("marks"), marks},
    };
}

} // namespace lmp::ui
