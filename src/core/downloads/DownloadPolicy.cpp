#include "core/downloads/DownloadPolicy.h"

#include "core/text/CaseFold.h"

#include <QLatin1StringView>

#include <algorithm>

namespace lmp::core {

DownloadItemState aggregateDownloadState(const QList<DownloadItemState>& states)
{
    if (states.contains(DownloadItemState::Downloading)) {
        return DownloadItemState::Downloading;
    }
    if (states.contains(DownloadItemState::Queued)) {
        return DownloadItemState::Queued;
    }
    if (states.contains(DownloadItemState::Paused)) {
        return DownloadItemState::Paused;
    }
    if (states.contains(DownloadItemState::Failed)) {
        return DownloadItemState::Failed;
    }
    return !states.isEmpty() ? DownloadItemState::Completed : DownloadItemState::Queued;
}

QString downloadExtensionForMediaType(QStringView mediaType)
{
    const QString folded = foldCaseAscii(mediaType);
    const auto has = [&folded](const char* part) { return folded.contains(QLatin1StringView(part)); };
    if (has("flac")) {
        return QStringLiteral(".flac");
    }
    if (has("mpeg")) {
        return QStringLiteral(".mp3");
    }
    if (has("mp4") || has("m4a")) {
        return QStringLiteral(".m4a");
    }
    if (has("ogg")) {
        return QStringLiteral(".ogg");
    }
    if (has("opus")) {
        return QStringLiteral(".opus");
    }
    if (has("wav")) {
        return QStringLiteral(".wav");
    }
    return QStringLiteral(".audio");
}

double downloadProgressPercent(std::uint64_t downloaded, std::uint64_t total) noexcept
{
    if (total == 0) {
        return 0.0;
    }
    const double percent = static_cast<double>(downloaded) * 100.0 / static_cast<double>(total);
    return std::clamp(percent, 0.0, 100.0);
}

bool downloadSchedulingAllowed(const DownloadSchedulingConditions& conditions) noexcept
{
    if (conditions.allPaused || conditions.shuttingDown) {
        return false;
    }
    if (conditions.avoidMeteredNetworks && conditions.isMetered) {
        return false;
    }
    if (!conditions.downloadOnBattery && conditions.onBattery) {
        return false;
    }
    return true;
}

} // namespace lmp::core
