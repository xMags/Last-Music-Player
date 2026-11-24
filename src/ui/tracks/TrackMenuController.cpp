#include "ui/tracks/TrackMenuController.h"

#include "core/library/TrackActions.h"
#include "core/net/UserMessages.h"
#include "ui/toasts/ToastQueue.h"
#include "ui/tracks/TrackMenuBuilder.h"

namespace lmp::ui {

TrackMenuController::TrackMenuController(core::TrackActions& actions, ToastQueue* toasts, QObject* parent)
    : QObject(parent)
    , m_actions(actions)
    , m_toasts(toasts)
{
}

void TrackMenuController::prepare(const core::Track& track, const QList<core::Track>& context,
                                  const QString& playlistKey)
{
    m_track = track;
    m_context = context;
    m_playlistKey = playlistKey;
    m_menu.setItems(TrackMenuBuilder::songMenu(
        track,
        {.play = m_actions.canPlay(track),
         .keep = m_actions.canKeep(track),
         .mix = m_actions.canStartMix(track),
         .share = m_actions.canShare(track)},
        playlistKey));
}

void TrackMenuController::prepareInPlaylist(const core::Track& track, const QList<core::Track>& context,
                                            const PlaylistPlace& place)
{
    m_track = track;
    m_context = context;
    m_playlistKey = place.key;
    m_menu.setItems(TrackMenuBuilder::songMenu(track,
                                               {.play = m_actions.canPlay(track),
                                                .keep = m_actions.canKeep(track),
                                                .moveUp = place.canMoveUp,
                                                .moveDown = place.canMoveDown,
                                                .mix = m_actions.canStartMix(track),
                                                .share = m_actions.canShare(track)},
                                               place.key));
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
        // The newest eight, with how many songs each holds.
        constexpr qsizetype MaxChoices = 8;
        m_playlistChoices.clear();
        for (const core::Track& playlist : m_actions.playlists()) {
            if (m_playlistChoices.size() == MaxChoices) {
                break;
            }
            m_playlistChoices.append(QVariantMap{{QStringLiteral("key"), playlist.sourceUrl},
                                                 {QStringLiteral("title"), playlist.title},
                                                 {QStringLiteral("count"), playlist.trackCount}});
        }
        emit playlistChoicesChanged();
        emit playlistMenuRequested();
    } else if (itemId == TrackMenuBuilder::RemoveFromPlaylist) {
        [[maybe_unused]] const core::Result<void> removed = m_actions.removeFromPlaylist(m_track, m_playlistKey);
    } else if (itemId == TrackMenuBuilder::MoveUp) {
        emit moveRequested(-1);
    } else if (itemId == TrackMenuBuilder::MoveDown) {
        emit moveRequested(1);
    } else if (itemId == TrackMenuBuilder::Artist) {
        m_actions.openArtist(m_track);
    } else if (itemId == TrackMenuBuilder::Album) {
        m_actions.openAlbum(m_track);
    } else if (itemId == TrackMenuBuilder::Mix) {
        m_actions.startMix(m_track);
    } else if (itemId == TrackMenuBuilder::Share) {
        m_actions.share(m_track);
    }
}

void TrackMenuController::createPlaylist(const QString& name)
{
    const QString title = name.trimmed();
    if (title.isEmpty()) {
        return;
    }
    const core::Result<QString> created = m_actions.addToNewPlaylist(m_track, title);
    if (m_toasts == nullptr) {
        return;
    }
    if (!created) {
        (void)m_toasts->show(core::net::userMessageOr(created.failure(), tr("Could not create the playlist.")), true);
        return;
    }
    (void)m_toasts->show(tr("Added to new playlist \"%1\"").arg(title));
}

void TrackMenuController::addToPlaylist(const QString& playlistKey)
{
    QString title;
    for (const QVariant& choice : std::as_const(m_playlistChoices)) {
        const QVariantMap fields = choice.toMap();
        if (fields.value(QStringLiteral("key")).toString() == playlistKey) {
            title = fields.value(QStringLiteral("title")).toString();
            break;
        }
    }
    // Only a playlist the popup offered.
    if (title.isEmpty()) {
        return;
    }
    const core::Result<void> added = m_actions.addToPlaylist(m_track, playlistKey);
    if (m_toasts == nullptr) {
        return;
    }
    if (!added) {
        (void)m_toasts->show(core::net::userMessageOr(added.failure(), tr("Could not add to the playlist.")), true);
        return;
    }
    (void)m_toasts->show(tr("Added to \"%1\"").arg(title));
}

} // namespace lmp::ui
