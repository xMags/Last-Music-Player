#include "pch.h"
#include "Backend/CatalogPresentation.h"

#include <algorithm>

namespace LastMusicPlayer::Backend
{
    std::vector<CatalogChartSection> BuildCatalogChartSections(CatalogDiscovery const& discovery)
    {
        CatalogChartSection explore{ L"explore", L"Explore charts" };
        CatalogChartSection cities{ L"cities", L"City Charts" };
        for (auto const& chart : discovery.Charts)
        {
            (chart.Ref.Kind == CatalogChartKind::City ? cities.Charts : explore.Charts).push_back(chart);
        }

        if (discovery.CityChartGroups)
        {
            cities.Charts.clear();
            auto const& groups = *discovery.CityChartGroups;
            cities.Charts.insert(
                cities.Charts.end(),
                groups.Indian.Charts.begin(),
                groups.Indian.Charts.end());
            cities.Charts.insert(
                cities.Charts.end(),
                groups.International.Charts.begin(),
                groups.International.Charts.end());
            cities.Partial = groups.Indian.Partial || groups.International.Partial;
        }

        std::vector<CatalogChartSection> sections;
        if (!explore.Charts.empty())
        {
            sections.push_back(std::move(explore));
        }
        if (!cities.Charts.empty())
        {
            sections.push_back(std::move(cities));
        }
        return sections;
    }

    std::size_t CatalogChartPreviewCount(std::size_t total) noexcept
    {
        return (std::min)(total, std::size_t{ 18 });
    }

    std::size_t CatalogMoodPreviewCount(std::size_t total) noexcept
    {
        return (std::min)(total, std::size_t{ 12 });
    }

    bool CatalogChartSectionShowsSeeAll(std::size_t total) noexcept
    {
        return total > 4;
    }

    void ApplyCatalogDetailTrackArtwork(CatalogResourceDetail& detail)
    {
        if (detail.Resource.Type != CatalogResourceType::Album
            || detail.Resource.ArtworkUrl.empty())
        {
            return;
        }

        for (auto& track : detail.Tracks)
        {
            // Album detail pages have one authoritative cover. Provider track
            // artwork can point at unrelated playback-search thumbnails, which
            // makes a single album look inconsistent and lower quality.
            track.ArtworkUrl = detail.Resource.ArtworkUrl;
        }
    }
}
