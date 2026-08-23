#include "pch.h"
#include "Backend/GenreArtworkRecipe.h"

#include <algorithm>
#include <cwctype>

namespace LastMusicPlayer::Backend
{
    namespace
    {
        GenreArtworkPrimitive Dot(double cx, double cy, double diameter)
        {
            return { GenreArtworkPrimitiveKind::Dot, cx - diameter / 2.0, cy - diameter / 2.0,
                diameter, diameter };
        }

        GenreArtworkPrimitive Ring(double cx, double cy, double diameter, double stroke, bool dashed = false)
        {
            return { GenreArtworkPrimitiveKind::Ring, cx - diameter / 2.0, cy - diameter / 2.0,
                diameter, diameter, stroke, diameter / 2.0, 0.0, dashed };
        }

        GenreArtworkPrimitive Oval(double cx, double cy, double width, double height, double stroke)
        {
            return { GenreArtworkPrimitiveKind::Oval, cx - width / 2.0, cy - height / 2.0,
                width, height, stroke, height / 2.0 };
        }

        GenreArtworkPrimitive Bar(double x, double y, double width, double height, double rotation = 0.0)
        {
            return { GenreArtworkPrimitiveKind::Bar, x, y, width, height, 0.0, height / 2.0, rotation };
        }

        GenreArtworkPrimitive OutlineBox(
            double cx, double cy, double size, double stroke, double radius, double rotation = 0.0)
        {
            return { GenreArtworkPrimitiveKind::OutlineBox, cx - size / 2.0, cy - size / 2.0,
                size, size, stroke, radius, rotation };
        }

        GenreArtworkPrimitive FilledSquare(
            double cx, double cy, double size, double radius, double rotation = 0.0)
        {
            return { GenreArtworkPrimitiveKind::FilledSquare, cx - size / 2.0, cy - size / 2.0,
                size, size, 0.0, radius, rotation };
        }

        GenreArtworkPrimitive OutlineRectangle(
            double cx, double cy, double width, double height, double stroke, double radius)
        {
            return { GenreArtworkPrimitiveKind::OutlineRectangle, cx - width / 2.0, cy - height / 2.0,
                width, height, stroke, radius };
        }

        GenreArtworkPrimitive Column(double x, double height, double width = 10.0)
        {
            return { GenreArtworkPrimitiveKind::Column, x, 84.0 - height, width, height,
                0.0, width / 2.0 };
        }

        GenreArtworkPrimitive Arch()
        {
            return { GenreArtworkPrimitiveKind::Arch, 15.0, 14.0, 60.0, 60.0, 3.0, 33.0 };
        }

        GenreArtworkPrimitive Triangle()
        {
            return { GenreArtworkPrimitiveKind::Triangle, 6.0, 14.0, 84.0, 70.0 };
        }

        GenreArtworkRecipe Recipe(
            wchar_t const* name,
            std::uint32_t start,
            std::uint32_t end,
            std::initializer_list<GenreArtworkPrimitive> primitives)
        {
            return { name, start, end, primitives };
        }
    }

    std::vector<GenreArtworkRecipe> const& AllGenreArtworkRecipes()
    {
        static const std::vector<GenreArtworkRecipe> recipes{
            Recipe(L"Pop", 0xFFFF5FBF, 0xFFA855F7,
                { Ring(48, 48, 74, 3), Dot(48, 48, 22) }),
            Recipe(L"Hip-Hop", 0xFFFF9330, 0xFFF43F5E,
                { Dot(44, 54, 68), Ring(44, 54, 82, 3), Bar(30, 10, 60, 8, 34) }),
            Recipe(L"Electronic", 0xFF22C1DC, 0xFF6366F1,
                { Column(12, 32), Column(29, 60), Column(46, 40), Column(63, 72), Column(80, 26) }),
            Recipe(L"Chill", 0xFF34D399, 0xFF06B6D4,
                { Ring(48, 48, 88, 2), Ring(48, 48, 60, 2), Ring(48, 48, 30, 2) }),
            Recipe(L"Rock", 0xFFF97316, 0xFFB91C1C,
                { OutlineBox(48, 48, 62, 3, 6, 45), Bar(20, 44, 56, 8) }),
            Recipe(L"Metal", 0xFF7C8798, 0xFF1E293B,
                { OutlineBox(48, 48, 66, 3, 4, 45), FilledSquare(48, 48, 24, 3, 45) }),
            Recipe(L"Jazz", 0xFFFBBF24, 0xFFB45309,
                { Dot(48, 18, 20), Dot(24, 66, 20), Dot(72, 66, 20) }),
            Recipe(L"Blues", 0xFF3B82F6, 0xFF1E3A8A,
                { Ring(48, 48, 78, 3), Bar(14, 44, 68, 8) }),
            Recipe(L"Classical", 0xFFD6B370, 0xFF8B5E3C,
                { Bar(8, 10, 80, 9), Column(20, 56, 12), Column(64, 56, 12), Bar(8, 76, 80, 9) }),
            Recipe(L"Country", 0xFFF59E0B, 0xFF65A30D,
                { Arch(), Bar(6, 80, 84, 9) }),
            Recipe(L"R&B", 0xFFC084FC, 0xFF7C3AED,
                { Ring(34, 48, 56, 3), Ring(62, 48, 56, 3) }),
            Recipe(L"Soul", 0xFFFB7185, 0xFF9F1239,
                { Dot(48, 30, 44), Bar(12, 62, 72, 12), Bar(26, 82, 44, 10) }),
            Recipe(L"Folk", 0xFF84CC16, 0xFF15803D, { Triangle() }),
            Recipe(L"Reggae", 0xFFFACC15, 0xFF16A34A,
                { Bar(8, 22, 80, 11), Bar(8, 43, 80, 11), Bar(8, 64, 80, 11) }),
            Recipe(L"Punk", 0xFFFB7185, 0xFF334155,
                { Bar(6, 43, 84, 10, 45), Bar(6, 43, 84, 10, -45) }),
            Recipe(L"Indie", 0xFF38BDF8, 0xFF8B5CF6,
                { Ring(42, 54, 70, 3), Dot(80, 16, 18) }),
            Recipe(L"Dance", 0xFFF472B6, 0xFFFB923C,
                { Dot(26, 26, 24), Dot(70, 26, 24), Dot(26, 70, 24), Dot(70, 70, 24) }),
            Recipe(L"House", 0xFF2DD4BF, 0xFF3B82F6,
                { OutlineBox(48, 48, 80, 3, 12), OutlineBox(48, 48, 34, 3, 6) }),
            Recipe(L"Techno", 0xFFA3E635, 0xFF0EA5E9,
                { Dot(20, 20, 13), Dot(48, 20, 13), Dot(76, 20, 13),
                  Dot(20, 48, 13), Dot(48, 48, 13), Dot(76, 48, 13),
                  Dot(20, 76, 13), Dot(48, 76, 13), Dot(76, 76, 13) }),
            Recipe(L"Ambient", 0xFF7DD3FC, 0xFFA78BFA,
                { Oval(48, 48, 92, 46, 3), Oval(48, 48, 56, 26, 3) }),
            Recipe(L"Lo-fi", 0xFFFDBA74, 0xFFC084FC,
                { OutlineRectangle(48, 48, 88, 58, 3, 12), Dot(32, 48, 16), Dot(64, 48, 16) }),
            Recipe(L"Soundtrack", 0xFF94A3B8, 0xFF4338CA,
                { OutlineRectangle(48, 48, 88, 62, 3, 8), Bar(16, 46, 64, 6) }),
            Recipe(L"World", 0xFFF97316, 0xFF14B8A6,
                { Ring(48, 48, 84, 3), Oval(48, 48, 38, 84, 3), Bar(8, 45, 80, 5) }),
            Recipe(L"Latin", 0xFFFB923C, 0xFFE11D48,
                { Oval(30, 40, 44, 34, 3), Oval(64, 66, 60, 40, 3), Dot(74, 26, 14) }),
            Recipe(L"Bollywood", 0xFFF43F5E, 0xFFF59E0B,
                { OutlineBox(48, 48, 82, 3, 6, 45), OutlineBox(48, 48, 44, 3, 4, 45) }),
            Recipe(L"Ghazal", 0xFF8B5CF6, 0xFF0EA5E9,
                { Oval(48, 48, 54, 88, 3), Dot(48, 48, 16) }),
            Recipe(L"Unknown Genre", 0xFFCBD5E1, 0xFF64748B,
                { Ring(48, 48, 78, 3, true) })
        };
        return recipes;
    }

    std::wstring NormalizeGenreArtworkName(std::wstring_view name)
    {
        auto const first = std::find_if_not(name.begin(), name.end(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        });
        auto const last = std::find_if_not(name.rbegin(), name.rend(), [](wchar_t character)
        {
            return std::iswspace(character) != 0;
        }).base();
        if (first >= last)
        {
            return {};
        }

        std::wstring normalized(first, last);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        return normalized;
    }

    GenreArtworkRecipe const& ResolveGenreArtworkRecipe(std::wstring_view name)
    {
        auto const normalized = NormalizeGenreArtworkName(name);
        auto const& recipes = AllGenreArtworkRecipes();
        auto const found = std::find_if(recipes.begin(), recipes.end(), [&](GenreArtworkRecipe const& recipe)
        {
            return NormalizeGenreArtworkName(recipe.Name) == normalized;
        });
        return found == recipes.end() ? recipes.back() : *found;
    }
}
