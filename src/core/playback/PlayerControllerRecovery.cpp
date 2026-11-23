// PlayerController and remote streams: what the user is told when a remote
// song cannot be played, and fetching a stream again when it fails mid-song.

#include "core/playback/PlayerController.h"

#include "core/playback/PlaybackClock.h"
#include "core/playback/TrackSourceResolver.h"

#include <QFuture>
#include <QString>

namespace lmp::core {

namespace {

// Where a reopened stream starts: just before the end at most, and from the
// start when it had barely begun.
[[nodiscard]] double resumeTarget(double position, double duration)
{
    double target = position;
    if (duration > 1.0 && target > duration - 1.0) {
        target = duration - 1.0;
    }
    return target > 0.5 ? target : 0.0;
}

} // namespace

void PlayerController::noticeResolveFailure(const Failure& failure)
{
    switch (failure.kind) {
    case FailureKind::NotAvailable:
        emit playbackNotice(PlaybackNotice::NeedsMusicSource, *m_current);
        return;
    case FailureKind::Cancelled:
        // The source changed under the request; nothing to tell.
        return;
    default:
        emit playbackNotice(PlaybackNotice::NoPlayableSource, *m_current);
        return;
    }
}

void PlayerController::recoverStream()
{
    const qint64 now = m_clock.nowMilliseconds();
    if (!m_lastStreamRecoveryMs || now - *m_lastStreamRecoveryMs > StreamRecoveryWindowMs) {
        m_streamRecoveries = 0;
    }
    m_lastStreamRecoveryMs = now;
    if (m_streamRecoveries >= StreamRecoveryAttempts) {
        pauseOnFailedStream();
        return;
    }
    ++m_streamRecoveries;

    // The failed source is forgotten so its later events are ignored; a
    // newer load makes this attempt's answer stale.
    m_audioLoadId = 0;
    observeHistory(false);
    setState(State::Loading);
    const double resumeAt = resumeTarget(m_position, m_duration);
    const RequestScope::Token token = m_loads.next();
    m_resolver->refresh(*m_current)
        .then(this, [this, token, resumeAt](Result<QUrl> source) { handleRecovered(token, source, resumeAt); })
        .onCanceled(this, [this, token, resumeAt]() {
            handleRecovered(token, Failure::of(FailureKind::Cancelled, QStringLiteral("The resolve was cancelled.")),
                            resumeAt);
        });
}

void PlayerController::handleRecovered(RequestScope::Token token, const Result<QUrl>& source, double resumeAt)
{
    if (!m_loads.isCurrent(token) || !m_current) {
        return; // a newer load started
    }
    if (!source) {
        // The song was playing a moment ago: stay on it so the user can try
        // again, rather than moving on.
        noticeResolveFailure(source.failure());
        pauseOnFailedStream();
        return;
    }
    setPosition(resumeAt);
    m_audioLoadId = m_audio->open(source.value(), m_wantsPlay, resumeAt);
    applyVolume();
}

void PlayerController::pauseOnFailedStream()
{
    m_audioLoadId = 0;
    m_wantsPlay = false;
    observeHistory(false);
    // Nothing is open now, so play loads the song again where it stood.
    setState(State::Paused);
}

} // namespace lmp::core
