#include "core/playback/PlayerAutoplayQueue.h"

#include "core/playback/PlayerController.h"

namespace lmp::core {

PlayerAutoplayQueue::PlayerAutoplayQueue(PlayerController& player)
    : m_player(player)
{
}

std::optional<Track> PlayerAutoplayQueue::currentTrack() const
{
    return m_player.currentTrack();
}

bool PlayerAutoplayQueue::autoplay() const
{
    return m_player.autoplay();
}

bool PlayerAutoplayQueue::repeatOff() const
{
    return m_player.repeatMode() == RepeatMode::Off;
}

qsizetype PlayerAutoplayQueue::upcomingCount() const
{
    return m_player.queue().upcomingCount();
}

QList<Track> PlayerAutoplayQueue::queuedTracks() const
{
    const QueueState& state = m_player.queue().state();
    return state.contextQueue + state.userQueue;
}

void PlayerAutoplayQueue::appendAutoplayTracks(const QList<Track>& tracks, bool resume)
{
    m_player.appendAutoplayTracks(tracks, resume);
}

void PlayerAutoplayQueue::playAlone(const Track& track)
{
    m_player.playTrack(track);
}

} // namespace lmp::core
