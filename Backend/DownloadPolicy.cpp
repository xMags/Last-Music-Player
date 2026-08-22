#include "pch.h"
#include "Backend/DownloadPolicy.h"

#include <algorithm>
#include <cwctype>

namespace LastMusicPlayer::Backend
{
    DownloadItemState AggregateDownloadState(
        std::vector<DownloadItemState> const& states)
    {
        if (std::find(states.begin(), states.end(), DownloadItemState::Downloading) != states.end())
            return DownloadItemState::Downloading;
        if (std::find(states.begin(), states.end(), DownloadItemState::Queued) != states.end())
            return DownloadItemState::Queued;
        if (std::find(states.begin(), states.end(), DownloadItemState::Paused) != states.end())
            return DownloadItemState::Paused;
        if (std::find(states.begin(), states.end(), DownloadItemState::Failed) != states.end())
            return DownloadItemState::Failed;
        return !states.empty()
            ? DownloadItemState::Completed
            : DownloadItemState::Queued;
    }

    std::wstring DownloadExtensionForMediaType(std::wstring mediaType)
    {
        std::transform(mediaType.begin(), mediaType.end(), mediaType.begin(), [](wchar_t character)
        {
            return static_cast<wchar_t>(std::towlower(character));
        });
        if (mediaType.find(L"flac") != std::wstring::npos) return L".flac";
        if (mediaType.find(L"mpeg") != std::wstring::npos) return L".mp3";
        if (mediaType.find(L"mp4") != std::wstring::npos || mediaType.find(L"m4a") != std::wstring::npos) return L".m4a";
        if (mediaType.find(L"ogg") != std::wstring::npos) return L".ogg";
        if (mediaType.find(L"opus") != std::wstring::npos) return L".opus";
        if (mediaType.find(L"wav") != std::wstring::npos) return L".wav";
        return L".audio";
    }

    double DownloadProgressPercent(std::uint64_t downloaded, std::uint64_t total) noexcept
    {
        if (total == 0) return 0.0;
        auto const percent = static_cast<double>(downloaded) * 100.0 / static_cast<double>(total);
        return (std::clamp)(percent, 0.0, 100.0);
    }

    bool DownloadSchedulingAllowed(
        bool onlyOnWifi,
        bool isWifi,
        bool downloadOnBattery,
        bool onBattery,
        bool allPaused,
        bool shuttingDown) noexcept
    {
        if (allPaused || shuttingDown) return false;
        if (onlyOnWifi && !isWifi) return false;
        if (!downloadOnBattery && onBattery) return false;
        return true;
    }
}
