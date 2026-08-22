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
    // The network rule exists to keep downloads off data the listener pays for.
    // That is a question about the connection's cost, not about whether it is a
    // radio: a wired desktop is the cheapest connection there is.
    [[nodiscard]] bool DownloadSchedulingAllowed(
        bool avoidMeteredNetworks,
        bool isMetered,
        bool downloadOnBattery,
        bool onBattery,
        bool allPaused,
        bool shuttingDown) noexcept;
}
