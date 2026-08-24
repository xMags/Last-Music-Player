#pragma once

#include <cwctype>
#include <string>
#include <string_view>

namespace LastMusicPlayer::Backend
{
    struct ArtistIdentity
    {
        std::wstring CatalogId;
        std::wstring SourceUrl;
        std::wstring Name;
        std::wstring Provider;
    };

    [[nodiscard]] inline std::wstring NormalizeArtistName(std::wstring_view value)
    {
        std::wstring normalized;
        normalized.reserve(value.size());
        bool pendingSpace = false;
        for (wchar_t character : value)
        {
            if (std::iswspace(character))
            {
                pendingSpace = !normalized.empty();
                continue;
            }
            if (pendingSpace)
            {
                normalized.push_back(L' ');
                pendingSpace = false;
            }
            normalized.push_back(static_cast<wchar_t>(std::towlower(character)));
        }
        return normalized;
    }

    [[nodiscard]] inline std::wstring ArtistIdentityKey(ArtistIdentity const& artist)
    {
        if (!artist.CatalogId.empty())
        {
            return L"catalog|" + NormalizeArtistName(artist.Provider) + L"|" + artist.CatalogId;
        }
        if (!artist.SourceUrl.empty())
        {
            return L"url|" + artist.SourceUrl;
        }
        auto const name = NormalizeArtistName(artist.Name);
        return name.empty() ? std::wstring{} : L"name|" + name;
    }

    [[nodiscard]] inline bool SameArtistIdentity(
        ArtistIdentity const& left,
        ArtistIdentity const& right)
    {
        if (!left.CatalogId.empty() || !right.CatalogId.empty())
        {
            auto const leftProvider = NormalizeArtistName(left.Provider);
            auto const rightProvider = NormalizeArtistName(right.Provider);
            return !left.CatalogId.empty()
                && !right.CatalogId.empty()
                && left.CatalogId == right.CatalogId
                && (leftProvider.empty() || rightProvider.empty() || leftProvider == rightProvider);
        }
        if (!left.SourceUrl.empty() || !right.SourceUrl.empty())
        {
            return !left.SourceUrl.empty()
                && !right.SourceUrl.empty()
                && left.SourceUrl == right.SourceUrl;
        }
        auto const leftName = NormalizeArtistName(left.Name);
        return !leftName.empty() && leftName == NormalizeArtistName(right.Name);
    }
}
