#pragma once

#include "core/playback/AutoplayService.h"

namespace lmp::core {

class PlayerController;

// The player as the queue autoplay tops up.
class PlayerAutoplayQueue final : public AutoplayQueue {
public:
    // `player` outlives this object.
    explicit PlayerAutoplayQueue(PlayerController& player);

    [[nodiscard]] std::optional<Track> currentTrack() const override;
    [[nodiscard]] bool autoplay() const override;
    [[nodiscard]] bool repeatOff() const override;
    [[nodiscard]] qsizetype upcomingCount() const override;
    [[nodiscard]] QList<Track> queuedTracks() const override;
    void appendAutoplayTracks(const QList<Track>& tracks, bool resume) override;
    void playAlone(const Track& track) override;

private:
    PlayerController& m_player;
};

} // namespace lmp::core
