#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace LastMusicPlayer::Backend
{
    enum class GenreArtworkPrimitiveKind
    {
        Dot,
        Ring,
        Oval,
        Bar,
        OutlineBox,
        FilledSquare,
        OutlineRectangle,
        Column,
        Arch,
        Triangle
    };

    struct GenreArtworkPrimitive
    {
        GenreArtworkPrimitiveKind Kind{ GenreArtworkPrimitiveKind::Dot };
        double X{};
        double Y{};
        double Width{};
        double Height{};
        double StrokeWidth{};
        double CornerRadius{};
        double Rotation{};
        bool Dashed{};
    };

    struct GenreArtworkRecipe
    {
        std::wstring Name;
        std::uint32_t GradientStart{};
        std::uint32_t GradientEnd{};
        std::vector<GenreArtworkPrimitive> Primitives;
    };

    [[nodiscard]] std::wstring NormalizeGenreArtworkName(std::wstring_view name);
    [[nodiscard]] GenreArtworkRecipe const& ResolveGenreArtworkRecipe(std::wstring_view name);
    [[nodiscard]] std::vector<GenreArtworkRecipe> const& AllGenreArtworkRecipes();
}
