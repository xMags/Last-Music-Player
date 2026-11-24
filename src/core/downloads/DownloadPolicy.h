#pragma once

#include <QList>
#include <QString>
#include <QStringView>

#include <cstdint>

namespace lmp::core {

enum class DownloadItemState {
    Queued,
    Downloading,
    Paused,
    Completed,
    Failed,
};

// The state a collection shows for its items: Downloading when any item is,
// else Queued, Paused, Failed in that order of precedence; Completed when all
// are; Queued for an empty collection.
[[nodiscard]] DownloadItemState aggregateDownloadState(const QList<DownloadItemState>& states);

// File extension (with the dot) for a response media type, matched by
// substring ignoring case; ".audio" when nothing matches.
[[nodiscard]] QString downloadExtensionForMediaType(QStringView mediaType);

// Percentage 0 to 100; 0 when the total is unknown (zero).
[[nodiscard]] double downloadProgressPercent(std::uint64_t downloaded, std::uint64_t total) noexcept;

// The space downloads take, as Settings writes it: "1.5 GB" (one decimal)
// from 1 GiB, whole "MB" from 1 MiB, whole "KB" from 1 KiB, else "<n> B",
// and "0 MB" for nothing.
[[nodiscard]] QString downloadSizeText(std::uint64_t bytes);

// The conditions the download scheduler checks before starting work.
struct DownloadSchedulingConditions {
    bool avoidMeteredNetworks = false;
    bool isMetered = false;
    bool downloadOnBattery = false;
    bool onBattery = false;
    bool allPaused = false;
    bool shuttingDown = false;
};

// Whether queued downloads may run now. The network rule keeps downloads off
// data the listener pays for: it is about the connection's cost, not about
// whether it is wireless, since a wired desktop is the cheapest connection.
[[nodiscard]] bool downloadSchedulingAllowed(const DownloadSchedulingConditions& conditions) noexcept;

} // namespace lmp::core
