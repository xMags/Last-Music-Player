#pragma once

#include "GenreArtworkTile.g.h"

namespace winrt::Last_Music_Player::implementation
{
    struct GenreArtworkTile : GenreArtworkTileT<GenreArtworkTile>
    {
        GenreArtworkTile();

        winrt::hstring GenreName() const noexcept { return m_genreName; }
        void GenreName(winrt::hstring const& value);
        bool IsBrowseBanner() const noexcept { return m_isBrowseBanner; }
        void IsBrowseBanner(bool value);

    private:
        void Render();

        winrt::hstring m_genreName;
        bool m_isBrowseBanner{ false };
    };
}

namespace winrt::Last_Music_Player::factory_implementation
{
    struct GenreArtworkTile : GenreArtworkTileT<GenreArtworkTile, implementation::GenreArtworkTile>
    {
    };
}
