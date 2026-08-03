#pragma once

#include "Backend/CatalogModels.h"

#include <cstddef>
#include <vector>

namespace LastMusicPlayer::Backend
{
    struct CatalogChartSection
    {
        winrt::hstring Id;
        winrt::hstring Title;
        std::vector<CatalogChartDescriptor> Charts;
        bool Partial{ false };
    };

    std::vector<CatalogChartSection> BuildCatalogChartSections(CatalogDiscovery const& discovery);
    std::size_t CatalogChartPreviewCount(std::size_t total) noexcept;
    std::size_t CatalogMoodPreviewCount(std::size_t total) noexcept;
    bool CatalogChartSectionShowsSeeAll(std::size_t total) noexcept;
    void ApplyCatalogDetailTrackArtwork(CatalogResourceDetail& detail);
}
