#pragma once

#include "Backend/CatalogModels.h"

#include <winrt/Windows.Foundation.h>

namespace LastMusicPlayer::Backend
{
    // Parsers for the catalog payloads. The wire shape is owned by another
    // service, so every one of these is defensive: an unknown resource type is
    // skipped, an absent field defaults, and no exception escapes. A malformed
    // or truncated payload yields an empty result, never a crash and never a
    // half-populated shelf. Callers distinguish "no data" from "request failed"
    // by whether the request itself threw.

    std::vector<CatalogStorefront> ParseCatalogStorefronts(winrt::hstring const& json) noexcept;
    CatalogDiscovery ParseCatalogDiscovery(
        winrt::hstring const& json,
        winrt::hstring const& requestedStorefront = {}) noexcept;
    CatalogChartPage ParseCatalogChartPage(
        winrt::hstring const& json,
        CatalogChartRef const& requestedChart) noexcept;
    // kind selects which header key is read: "albums" reads `album`,
    // "playlists" reads `playlist`.
    CatalogResourceDetail ParseCatalogResourceDetail(
        winrt::hstring const& json,
        winrt::hstring const& kind) noexcept;
}
