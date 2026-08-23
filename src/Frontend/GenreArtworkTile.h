#pragma once

#include "GenreArtworkTile.g.h"

namespace winrt::Last_Music_Player::implementation
{
    struct GenreArtworkTile : GenreArtworkTileT<GenreArtworkTile>
    {
        GenreArtworkTile();

        winrt::hstring GenreName() const noexcept { return m_genreName; }
        void GenreName(winrt::hstring const& value);

    private:
        void Render();

        winrt::hstring m_genreName;
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct GenreArtworkTile : GenreArtworkTileT<GenreArtworkTile, implementation::GenreArtworkTile>
    {
    };
}
