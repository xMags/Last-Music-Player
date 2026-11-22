#include "core/library/GenreArtworkRecipe.h"

#include "core/text/CaseFold.h"

#include <algorithm>
#include <initializer_list>

namespace lmp::core {

namespace {

using Kind = GenreArtworkPrimitiveKind;

// The builders take centre points where the design does and turn them into
// bounding boxes, so the recipe table below reads like the design.

GenreArtworkPrimitive dot(double cx, double cy, double diameter)
{
    return {Kind::Dot, cx - diameter / 2.0, cy - diameter / 2.0, diameter, diameter};
}

GenreArtworkPrimitive ring(double cx, double cy, double diameter, double stroke, bool dashed = false)
{
    return {Kind::Ring, cx - diameter / 2.0, cy - diameter / 2.0, diameter, diameter, stroke, diameter / 2.0, 0.0,
            dashed};
}

GenreArtworkPrimitive oval(double cx, double cy, double width, double height, double stroke)
{
    return {Kind::Oval, cx - width / 2.0, cy - height / 2.0, width, height, stroke, height / 2.0};
}

GenreArtworkPrimitive bar(double x, double y, double width, double height, double rotation = 0.0)
{
    return {Kind::Bar, x, y, width, height, 0.0, height / 2.0, rotation};
}

GenreArtworkPrimitive outlineBox(double cx, double cy, double size, double stroke, double radius,
                                 double rotation = 0.0)
{
    return {Kind::OutlineBox, cx - size / 2.0, cy - size / 2.0, size, size, stroke, radius, rotation};
}

GenreArtworkPrimitive filledSquare(double cx, double cy, double size, double radius, double rotation = 0.0)
{
    return {Kind::FilledSquare, cx - size / 2.0, cy - size / 2.0, size, size, 0.0, radius, rotation};
}

GenreArtworkPrimitive outlineRectangle(double cx, double cy, double width, double height, double stroke,
                                       double radius)
{
    return {Kind::OutlineRectangle, cx - width / 2.0, cy - height / 2.0, width, height, stroke, radius};
}

// Columns stand on a common baseline at y = 84.
GenreArtworkPrimitive column(double x, double height, double width = 10.0)
{
    return {Kind::Column, x, 84.0 - height, width, height, 0.0, width / 2.0};
}

GenreArtworkPrimitive arch()
{
    return {Kind::Arch, 15.0, 14.0, 60.0, 60.0, 3.0, 33.0};
}

GenreArtworkPrimitive triangle()
{
    return {Kind::Triangle, 6.0, 14.0, 84.0, 70.0};
}

GenreArtworkRecipe recipe(const char* name, std::uint32_t start, std::uint32_t end,
                          std::initializer_list<GenreArtworkPrimitive> primitives)
{
    return {QString::fromLatin1(name), start, end, QList<GenreArtworkPrimitive>(primitives)};
}

QList<GenreArtworkRecipe> buildRecipes()
{
    return {
        recipe("Pop", 0xFFFF5FBF, 0xFFA855F7, {ring(48, 48, 74, 3), dot(48, 48, 22)}),
        recipe("Hip-Hop", 0xFFFF9330, 0xFFF43F5E, {dot(44, 54, 68), ring(44, 54, 82, 3), bar(30, 10, 60, 8, 34)}),
        recipe("Electronic", 0xFF22C1DC, 0xFF6366F1,
               {column(12, 32), column(29, 60), column(46, 40), column(63, 72), column(80, 26)}),
        recipe("Chill", 0xFF34D399, 0xFF06B6D4, {ring(48, 48, 88, 2), ring(48, 48, 60, 2), ring(48, 48, 30, 2)}),
        recipe("Rock", 0xFFF97316, 0xFFB91C1C, {outlineBox(48, 48, 62, 3, 6, 45), bar(20, 44, 56, 8)}),
        recipe("Metal", 0xFF7C8798, 0xFF1E293B, {outlineBox(48, 48, 66, 3, 4, 45), filledSquare(48, 48, 24, 3, 45)}),
        recipe("Jazz", 0xFFFBBF24, 0xFFB45309, {dot(48, 18, 20), dot(24, 66, 20), dot(72, 66, 20)}),
        recipe("Blues", 0xFF3B82F6, 0xFF1E3A8A, {ring(48, 48, 78, 3), bar(14, 44, 68, 8)}),
        recipe("Classical", 0xFFD6B370, 0xFF8B5E3C,
               {bar(8, 10, 80, 9), column(20, 56, 12), column(64, 56, 12), bar(8, 76, 80, 9)}),
        recipe("Country", 0xFFF59E0B, 0xFF65A30D, {arch(), bar(6, 80, 84, 9)}),
        recipe("R&B", 0xFFC084FC, 0xFF7C3AED, {ring(34, 48, 56, 3), ring(62, 48, 56, 3)}),
        recipe("Soul", 0xFFFB7185, 0xFF9F1239, {dot(48, 30, 44), bar(12, 62, 72, 12), bar(26, 82, 44, 10)}),
        recipe("Folk", 0xFF84CC16, 0xFF15803D, {triangle()}),
        recipe("Reggae", 0xFFFACC15, 0xFF16A34A, {bar(8, 22, 80, 11), bar(8, 43, 80, 11), bar(8, 64, 80, 11)}),
        recipe("Punk", 0xFFFB7185, 0xFF334155, {bar(6, 43, 84, 10, 45), bar(6, 43, 84, 10, -45)}),
        recipe("Indie", 0xFF38BDF8, 0xFF8B5CF6, {ring(42, 54, 70, 3), dot(80, 16, 18)}),
        recipe("Dance", 0xFFF472B6, 0xFFFB923C, {dot(26, 26, 24), dot(70, 26, 24), dot(26, 70, 24), dot(70, 70, 24)}),
        recipe("House", 0xFF2DD4BF, 0xFF3B82F6, {outlineBox(48, 48, 80, 3, 12), outlineBox(48, 48, 34, 3, 6)}),
        recipe("Techno", 0xFFA3E635, 0xFF0EA5E9,
               {dot(20, 20, 13), dot(48, 20, 13), dot(76, 20, 13), dot(20, 48, 13), dot(48, 48, 13), dot(76, 48, 13),
                dot(20, 76, 13), dot(48, 76, 13), dot(76, 76, 13)}),
        recipe("Ambient", 0xFF7DD3FC, 0xFFA78BFA, {oval(48, 48, 92, 46, 3), oval(48, 48, 56, 26, 3)}),
        recipe("Lo-fi", 0xFFFDBA74, 0xFFC084FC,
               {outlineRectangle(48, 48, 88, 58, 3, 12), dot(32, 48, 16), dot(64, 48, 16)}),
        recipe("Soundtrack", 0xFF94A3B8, 0xFF4338CA, {outlineRectangle(48, 48, 88, 62, 3, 8), bar(16, 46, 64, 6)}),
        recipe("World", 0xFFF97316, 0xFF14B8A6, {ring(48, 48, 84, 3), oval(48, 48, 38, 84, 3), bar(8, 45, 80, 5)}),
        recipe("Latin", 0xFFFB923C, 0xFFE11D48, {oval(30, 40, 44, 34, 3), oval(64, 66, 60, 40, 3), dot(74, 26, 14)}),
        recipe("Bollywood", 0xFFF43F5E, 0xFFF59E0B,
               {outlineBox(48, 48, 82, 3, 6, 45), outlineBox(48, 48, 44, 3, 4, 45)}),
        recipe("Ghazal", 0xFF8B5CF6, 0xFF0EA5E9, {oval(48, 48, 54, 88, 3), dot(48, 48, 16)}),
        recipe("Unknown Genre", 0xFFCBD5E1, 0xFF64748B, {ring(48, 48, 78, 3, true)}),
    };
}

} // namespace

const QList<GenreArtworkRecipe>& allGenreArtworkRecipes()
{
    // Immutable after its thread-safe first initialisation.
    static const QList<GenreArtworkRecipe> recipes = buildRecipes();
    return recipes;
}

QString normalizeGenreArtworkName(QStringView name)
{
    return foldCaseAscii(name.trimmed());
}

const GenreArtworkRecipe& resolveGenreArtworkRecipe(QStringView name)
{
    const QString normalized = normalizeGenreArtworkName(name);
    const QList<GenreArtworkRecipe>& recipes = allGenreArtworkRecipes();
    const auto found = std::find_if(recipes.cbegin(), recipes.cend(), [&normalized](const GenreArtworkRecipe& candidate) {
        return normalizeGenreArtworkName(candidate.name) == normalized;
    });
    return found == recipes.cend() ? recipes.last() : *found;
}

} // namespace lmp::core
