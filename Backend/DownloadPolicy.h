#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend
{
    enum class DownloadItemState
    {
        Queued,
        Downloading,
        Paused,
        Completed,
        Failed,
    };

    [[nodiscard]] DownloadItemState AggregateDownloadState(
        std::vector<DownloadItemState> const& states);
    [[nodiscard]] std::wstring DownloadExtensionForMediaType(std::wstring mediaType);
    [[nodiscard]] double DownloadProgressPercent(
        std::uint64_t downloaded,
        std::uint64_t total) noexcept;
    [[nodiscard]] bool DownloadSchedulingAllowed(
        bool onlyOnWifi,
        bool isWifi,
        bool downloadOnBattery,
        bool onBattery,
        bool allPaused,
        bool shuttingDown) noexcept;
}
