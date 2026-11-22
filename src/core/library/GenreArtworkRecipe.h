#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <cstdint>

namespace lmp::core {

enum class GenreArtworkPrimitiveKind {
    Dot,
    Ring,
    Oval,
    Bar,
    OutlineBox,
    FilledSquare,
    OutlineRectangle,
    Column,
    Arch,
    Triangle,
};

// One mark of a genre tile, in the tile's design units. x and y are the
// top-left corner of the mark's bounding box; rotation is in degrees.
struct GenreArtworkPrimitive {
    GenreArtworkPrimitiveKind kind = GenreArtworkPrimitiveKind::Dot;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double strokeWidth = 0.0;
    double cornerRadius = 0.0;
    double rotation = 0.0;
    bool dashed = false;
};

// The drawn tile of a genre: a two-colour gradient (0xAARRGGBB) and its marks.
struct GenreArtworkRecipe {
    QString name;
    std::uint32_t gradientStart = 0;
    std::uint32_t gradientEnd = 0;
    QList<GenreArtworkPrimitive> primitives;
};

// Trimmed and lower-cased, the form genre names are matched in.
[[nodiscard]] QString normalizeGenreArtworkName(QStringView name);

// The recipe whose name matches `name` after normalising both; the
// "Unknown Genre" recipe (always the last one) when none does.
[[nodiscard]] const GenreArtworkRecipe& resolveGenreArtworkRecipe(QStringView name);

// Every recipe in its fixed order. The list is built once and never changes.
[[nodiscard]] const QList<GenreArtworkRecipe>& allGenreArtworkRecipes();

} // namespace lmp::core
