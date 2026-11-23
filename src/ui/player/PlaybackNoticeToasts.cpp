#include "ui/player/PlaybackNoticeToasts.h"

#include "ui/toasts/ToastQueue.h"

namespace lmp::ui {

PlaybackNoticeToasts::PlaybackNoticeToasts(core::PlayerController& player, ToastQueue& toasts, QObject* parent)
    : QObject(parent)
    , m_toasts(toasts)
{
    connect(&player, &core::PlayerController::playbackNotice, this,
            [this](core::PlayerController::PlaybackNotice notice, const core::Track& track) {
                m_toasts.show(text(notice, track.title), true);
            });
}

QString PlaybackNoticeToasts::text(core::PlayerController::PlaybackNotice notice, const QString& title)
{
    using Notice = core::PlayerController::PlaybackNotice;
    switch (notice) {
    case Notice::NeedsMusicSource:
        return tr("Select a remote integration to play this track.");
    case Notice::NoPlayableSource:
        return title.isEmpty() ? tr("No playable source for this track.")
                               : tr("No playable source for \"%1\".").arg(title);
    case Notice::RemoteFailuresStopped:
        return tr("Could not play the next few tracks. Playback stopped.");
    }
    return {};
}

} // namespace lmp::ui
