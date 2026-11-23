#include "ui/tracks/TrackMenuController.h"

#include "core/library/TrackActions.h"
#include "ui/tracks/TrackMenuBuilder.h"

namespace lmp::ui {

TrackMenuController::TrackMenuController(core::TrackActions& actions, QObject* parent)
    : QObject(parent)
    , m_actions(actions)
{
}

void TrackMenuController::prepare(const core::Track& track, const QList<core::Track>& context,
                                  const QString& playlistKey)
{
    m_track = track;
    m_context = context;
    m_playlistKey = playlistKey;
    m_menu.setItems(TrackMenuBuilder::songMenu(
        track, {.play = m_actions.canPlay(track), .keep = m_actions.canKeep(track)}, playlistKey));
}

void TrackMenuController::trigger(const QString& itemId)
{
    if (itemId == TrackMenuBuilder::Play) {
        m_actions.play(m_track, m_context);
    } else if (itemId == TrackMenuBuilder::PlayNext) {
        m_actions.playNext(m_track);
    } else if (itemId == TrackMenuBuilder::Queue) {
        m_actions.addToQueue(m_track);
    } else if (itemId == TrackMenuBuilder::Like) {
        if (const std::optional<bool> liked = m_actions.toggleLike(m_track)) {
            m_track.liked = *liked;
        }
    } else if (itemId == TrackMenuBuilder::AddToPlaylist) {
        m_playlistMenu.setItems(TrackMenuBuilder::playlistMenu(m_actions.playlists()));
        emit playlistMenuRequested();
    } else if (itemId == TrackMenuBuilder::NewPlaylist) {
        emit newPlaylistRequested();
    } else if (itemId.startsWith(TrackMenuBuilder::PlaylistPrefix)) {
        [[maybe_unused]] const core::Result<void> added =
            m_actions.addToPlaylist(m_track, itemId.mid(TrackMenuBuilder::PlaylistPrefix.size()));
    } else if (itemId == TrackMenuBuilder::RemoveFromPlaylist) {
        [[maybe_unused]] const core::Result<void> removed = m_actions.removeFromPlaylist(m_track, m_playlistKey);
    } else if (itemId == TrackMenuBuilder::Artist) {
        m_actions.openArtist(m_track);
    } else if (itemId == TrackMenuBuilder::Album) {
        m_actions.openAlbum(m_track);
    }
}

void TrackMenuController::createPlaylist(const QString& name)
{
    [[maybe_unused]] const core::Result<QString> created = m_actions.addToNewPlaylist(m_track, name);
}

} // namespace lmp::ui
