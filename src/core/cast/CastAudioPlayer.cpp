#include "core/cast/CastAudioPlayer.h"

#include "core/cast/CastSession.h"

#include <algorithm>
#include <cmath>

namespace lmp::core::cast {

CastAudioPlayer::CastAudioPlayer(QObject* parent)
    : platform::IAudioPlayer(parent)
{
    m_poll.setInterval(StatusPollMs);
    connect(&m_poll, &QTimer::timeout, this, [this]() {
        if (m_session != nullptr) {
            m_session->requestStatus();
        }
    });
    m_tick.setInterval(PositionTickMs);
    connect(&m_tick, &QTimer::timeout, this, &CastAudioPlayer::tick);
}

void CastAudioPlayer::setSession(CastSession* session)
{
    if (m_session == session) {
        return;
    }
    if (m_session != nullptr) {
        disconnect(m_session, nullptr, this, nullptr);
    }
    m_session = session;
    m_mediaSessionId = 0;
    m_previousMediaSessionId = 0;
    if (session != nullptr) {
        connect(session, &CastSession::mediaStatus, this, &CastAudioPlayer::applyStatus);
    } else {
        m_poll.stop();
        m_tick.stop();
        m_state = State::Idle;
    }
}

void CastAudioPlayer::setMetadata(const QString& title, const QString& artist, const QString& artworkUrl)
{
    m_title = title;
    m_artist = artist;
    m_artworkUrl = artworkUrl;
}

quint64 CastAudioPlayer::open(const QUrl& source, bool autoplay, double startAtSeconds)
{
    const quint64 id = ++m_loadId;
    m_previousMediaSessionId = m_mediaSessionId;
    m_mediaSessionId = 0;
    m_duration = 0.0;
    m_position = std::isfinite(startAtSeconds) ? std::max(0.0, startAtSeconds) : 0.0;
    m_state = State::Loading;
    m_poll.start();
    m_tick.stop();
    // Never from inside open().
    QTimer::singleShot(0, this, [this, id]() {
        if (id == m_loadId && m_state == State::Loading) {
            emit stateChanged(id, State::Loading);
        }
    });
    if (m_session == nullptr) {
        QTimer::singleShot(0, this, [this, id]() {
            if (id != m_loadId) {
                return;
            }
            emit errorOccurred(id, ErrorKind::OutputDevice, QStringLiteral("No cast session."));
            report(State::Error);
        });
        return id;
    }
    m_session->load(CastLoadRequest{.url = source.toString(QUrl::FullyEncoded),
                                    .title = m_title,
                                    .artist = m_artist,
                                    .artworkUrl = m_artworkUrl,
                                    .startAtSeconds = m_position,
                                    .autoplay = autoplay});
    return id;
}

void CastAudioPlayer::play()
{
    if (m_session == nullptr || m_loadId == 0) {
        return;
    }
    if (m_state == State::Ended) {
        m_session->seek(0.0);
    }
    m_session->play();
}

void CastAudioPlayer::pause()
{
    if (m_session != nullptr) {
        m_session->pause();
    }
}

void CastAudioPlayer::seek(double seconds)
{
    if (!std::isfinite(seconds) || m_session == nullptr) {
        return;
    }
    const double target = m_duration > 0.0 ? std::clamp(seconds, 0.0, m_duration) : std::max(0.0, seconds);
    m_position = target;
    m_sinceStatus.restart();
    m_session->seek(target);
    emit positionChanged(m_loadId, target);
}

void CastAudioPlayer::stop()
{
    m_poll.stop();
    m_tick.stop();
    if (m_session != nullptr && m_state != State::Idle) {
        m_session->stop();
    }
    if (m_state != State::Idle) {
        report(State::Idle);
    }
}

void CastAudioPlayer::setVolume(double volume)
{
    if (m_session != nullptr) {
        m_session->setVolume(volume);
    }
}

void CastAudioPlayer::setOutputDevice(const QString& deviceId)
{
    Q_UNUSED(deviceId) // the device plays on its own speakers
}

void CastAudioPlayer::applyStatus(const CastMediaStatus& status)
{
    if (m_loadId == 0 || m_state == State::Idle || m_state == State::Error) {
        return;
    }
    // The media of an earlier open, still reporting (its end or its cut-off).
    if (status.mediaSessionId != 0 && status.mediaSessionId == m_previousMediaSessionId) {
        return;
    }
    if (status.mediaSessionId != 0) {
        m_mediaSessionId = status.mediaSessionId;
    }
    const quint64 id = m_loadId;
    if (status.duration && *status.duration != m_duration) {
        m_duration = *status.duration;
        emit durationChanged(id, m_duration);
    }
    if (status.playerState == QLatin1StringView("IDLE")) {
        if (status.idleReason == QLatin1StringView("FINISHED")) {
            m_poll.stop();
            m_tick.stop();
            m_position = m_duration;
            emit positionChanged(id, m_duration);
            report(State::Ended);
        } else if (status.idleReason == QLatin1StringView("ERROR")) {
            m_poll.stop();
            m_tick.stop();
            emit errorOccurred(id, ErrorKind::Network, QStringLiteral("The cast device could not play the song."));
            report(State::Error);
        }
        // CANCELLED and INTERRUPTED come from a stop or a newer load.
        return;
    }
    if (status.playerState == QLatin1StringView("LOADING") || status.playerState.isEmpty()) {
        return;
    }
    m_position = std::isfinite(status.currentTime) ? std::max(0.0, status.currentTime) : m_position;
    m_sinceStatus.restart();
    emit positionChanged(id, m_position);
    if (status.playerState == QLatin1StringView("PAUSED")) {
        m_tick.stop();
        report(State::Paused);
        return;
    }
    // PLAYING, or BUFFERING while asked to play.
    if (!m_poll.isActive()) {
        m_poll.start();
    }
    if (status.playerState == QLatin1StringView("PLAYING")) {
        m_tick.start();
    } else {
        m_tick.stop();
    }
    report(State::Playing);
}

void CastAudioPlayer::report(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged(m_loadId, state);
}

void CastAudioPlayer::tick()
{
    if (m_state != State::Playing || !m_sinceStatus.isValid()) {
        return;
    }
    double position = m_position + static_cast<double>(m_sinceStatus.elapsed()) / 1000.0;
    if (m_duration > 0.0) {
        position = std::min(position, m_duration);
    }
    emit positionChanged(m_loadId, position);
}

} // namespace lmp::core::cast
