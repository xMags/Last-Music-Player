#pragma once

#include "Backend/CatalogModels.h"
#include "Backend/RemoteDetailContract.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LastMusicPlayer::Backend
{
    enum class AlbumLoadRoute
    {
        LocalLibrary,
        AccountCatalog,
        ProviderDetail,
    };

    struct AlbumTrackModel
    {
        CatalogItem Catalog;
        winrt::hstring SourceKind{ L"remote" };
        winrt::hstring RemoteProvider;
        winrt::hstring SourceLabel{ L"Catalog" };
        std::int64_t LocalCatalogId{ 0 };
        winrt::hstring DateAdded;
        double DateAddedSortKey{ 0.0 };
        double ResumePositionSeconds{ 0.0 };
        winrt::hstring DurationText;
        std::optional<bool> IsLiked;
        std::optional<bool> IsOffline;
        std::optional<bool> IsInLibrary;
    };

    struct AlbumDetailModel
    {
        winrt::hstring Storefront;
        CatalogItem Resource;
        std::vector<AlbumTrackModel> Tracks;
    };

    struct AlbumLoadRequest
    {
        AlbumLoadRoute Route{ AlbumLoadRoute::ProviderDetail };
        RemoteDetailIdentity Identity;
        winrt::hstring Storefront;
        CatalogItem Release;
    };

    [[nodiscard]] inline bool IsAppleMusicProvider(
        winrt::hstring const& provider) noexcept
    {
        std::wstring normalized{ provider.c_str() };
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value)
        {
            return static_cast<wchar_t>(std::towlower(value));
        });
        return normalized == L"apple-music" || normalized == L"apple music";
    }

    [[nodiscard]] inline AlbumLoadRoute SelectArtistAlbumLoadRoute(
        bool localRelease,
        bool accountMode,
        winrt::hstring const& provider,
        winrt::hstring const& catalogId) noexcept
    {
        if (localRelease)
        {
            return AlbumLoadRoute::LocalLibrary;
        }
        if (accountMode && IsAppleMusicProvider(provider) && !catalogId.empty())
        {
            return AlbumLoadRoute::AccountCatalog;
        }
        return AlbumLoadRoute::ProviderDetail;
    }

    [[nodiscard]] inline std::wstring AlbumDetailCacheKey(
        std::wstring_view scopeKey,
        AlbumLoadRequest const& request)
    {
        if (scopeKey.empty() || request.Route == AlbumLoadRoute::LocalLibrary)
        {
            return {};
        }

        auto appendPart = [](std::wstring& key, std::wstring_view name, winrt::hstring const& value)
        {
            key += L"|";
            key += name;
            key += L":";
            key += std::to_wstring(value.size());
            key += L":";
            key += value.c_str();
        };

        std::wstring key{ scopeKey };
        if (request.Route == AlbumLoadRoute::AccountCatalog)
        {
            auto storefront = std::wstring{ request.Storefront.c_str() };
            std::transform(storefront.begin(), storefront.end(), storefront.begin(), [](wchar_t value)
            {
                return static_cast<wchar_t>(std::towlower(value));
            });
            appendPart(key, L"route", L"account-catalog");
            appendPart(key, L"storefront", winrt::hstring{ storefront });
            appendPart(key, L"catalog", request.Identity.CatalogId);
            return request.Identity.CatalogId.empty() || storefront.empty() ? std::wstring{} : key;
        }

        appendPart(key, L"route", L"provider-detail");
        if (!request.Identity.CatalogId.empty())
        {
            appendPart(key, L"catalog", request.Identity.CatalogId);
            return key;
        }
        if (!request.Identity.SourceUrl.empty())
        {
            appendPart(key, L"source", request.Identity.SourceUrl);
            return key;
        }
        if (!request.Identity.Name.empty())
        {
            appendPart(key, L"name", request.Identity.Name);
            return key;
        }
        return {};
    }

    [[nodiscard]] inline AlbumDetailModel AlbumDetailFromCatalog(
        CatalogResourceDetail const& detail)
    {
        AlbumDetailModel result;
        result.Storefront = detail.Storefront;
        result.Resource = detail.Resource;
        result.Tracks.reserve(detail.Tracks.size());
        for (auto const& track : detail.Tracks)
        {
            AlbumTrackModel model;
            model.Catalog = track;
            model.RemoteProvider = track.Provider;
            result.Tracks.push_back(std::move(model));
        }
        return result;
    }

    [[nodiscard]] inline AlbumDetailModel AlbumDetailFromProvider(
        RemoteAlbumDetail const& detail,
        winrt::hstring const& storefront = {})
    {
        AlbumDetailModel result;
        result.Storefront = storefront;
        result.Resource = detail.Release.Release;
        result.Tracks.reserve(detail.Tracks.size());
        for (auto const& track : detail.Tracks)
        {
            AlbumTrackModel model;
            model.Catalog = track;
            model.RemoteProvider = track.Provider;
            model.SourceLabel = L"Provider";
            result.Tracks.push_back(std::move(model));
        }
        return result;
    }
}
